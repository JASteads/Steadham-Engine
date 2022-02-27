#include <windows.h>
#include <xinput.h> /* Needed for Windows controller input */
#include <dsound.h> /* Needed for DirectSound */
#include <strsafe.h>

#include "engine.h"
#include "win32_engine.h"

global long long frequency;
global LPDIRECTSOUNDBUFFER second_sbuffer; /* This is for sound. Subject for removal of globalness. */

/* TO DO ... */
/*	
	- Save Data and Data Locations
	- Handle for the .exe
	- Asset loading path
	- Multithreading
	- Raw input (support multiple keyboards ; questionable)
	- Sleep/Time_begin_period();
	- Clip_cursor();
	- Fullscreen support
	- WM_SETCURSOR (cursor visibility)
	- Query_cancel_autoplay();
	- WM_ACTIVATEAPP
	- Blit speed improvements
	- Hardware acceleration (OpenGL, DirectX, etc.)
	- Get_keyboard_layout();								*/

/* *** UTILITIES *** */
local int Str_length(char *string)
{
	int count = 0;
	while (*string++)
		++count;
	return count;
}
local void Concat_strings(char *source_a, size_t source_count_a, char *source_b, size_t source_count_b, char *dest)
{
	char *sources[2] = { source_a, source_b };
	size_t source_counts[2] = { source_count_a, source_count_b };

	for (int source_index = 0; source_index < ARRAY_COUNT(sources); source_index++)
		for (int i = 0; i < source_counts[source_index]; ++i)
			*dest++ = *sources[source_index]++;
	*dest++ = 0;
}
local void Win32_get_build_exe_file_path(Win32_state *state, char *file_name, char *dest)
{
	Concat_strings(state->exe_name, state->last_slash_plus_one - state->exe_name,
		file_name, Str_length(file_name), dest);
}

/* *** RENDERING *** */
local void Win32_resize_DIB_sect(Win32_pixel_buffer *buffer, int w, int h) /* DIB - Device-independent Bitmap */
{
	/* Consider not freeing first, free after, then free first if fails */

	if (buffer->memory) VirtualFree(buffer->memory, 0, MEM_RELEASE);

	buffer->w = w;
	buffer->h = h;
	buffer->info.bmiHeader.biSize = sizeof(buffer->info.bmiHeader);
	buffer->info.bmiHeader.biWidth = buffer->w;
	buffer->info.bmiHeader.biHeight = -buffer->h; /* Negative to render top-down */
	buffer->info.bmiHeader.biPlanes = 1;
	buffer->info.bmiHeader.biBitCount = 32;
	buffer->info.bmiHeader.biCompression = BI_RGB;

	/*	4 bytes times the width and height; 32 bits as declared in 'biBitCount.'
		Returns a void pointer that allocates data for us to use.
		We don't need to execute anything with this data, so we'll simply enable
		reading and writing for it.													*/
	buffer->memory = VirtualAlloc(0, (size_t)((buffer->bpp * (buffer->w * buffer->h))),
		MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	buffer->pitch = w * buffer->bpp;
}

/* TESTING: Allocates memory based on the size of the object that's written to. Can also write/read a dummy a file. */

D_PLATFORM_FREE_FILE_MEM(D_platform_free_file_mem)
{
	if (memory) VirtualFree(memory, 0, MEM_RELEASE);
}
D_PLATFORM_READ(D_platform_read)
{
	D_read_result result = {};
	HANDLE handle = CreateFile(file_name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (handle != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER file_size;
		if (GetFileSizeEx(handle, &file_size))
		{
			/* Don't use VirtualAlloc for smaller files. Use HeapAlloc isntead. */
			result.content = VirtualAlloc(0, (size_t)file_size.QuadPart, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			DWORD read_bytes;
			if (result.content)
			{	
				ReadFile(handle, result.content, (DWORD)file_size.QuadPart, &read_bytes, 0);
				result.size = file_size.QuadPart;
			}
			else
			{
				D_platform_free_file_mem(threading, result.content);
				result.content = 0;
				/* Will fail if file is over 4 GB. */
			}
		}
		else { /* Logging */ }
		CloseHandle(handle);
	}
	return result;
}
D_PLATFORM_WRITE(D_platform_write)
{
	bool success = false;
	HANDLE handle = CreateFile(file_name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	if (handle != INVALID_HANDLE_VALUE)
	{
		DWORD read_bytes;
		if (WriteFile(handle, file->content, (uint)file->size, &read_bytes, 0))
			success = (read_bytes == (uint)file->size);
		else
		{
			D_platform_free_file_mem(threading, file->content);
			file->content = 0;
			/* Will fail if file is over 4 GB. */
		}
		CloseHandle(handle);
	}
	return success;
}

/* Copies the buffer parameter to the window. */
local void Win32_update_win(Win32_pixel_buffer *buffer, HWND win_handle, HDC context)
{
	RECT win_rect;
	GetClientRect(win_handle, &win_rect);

	int win_w = win_rect.right - win_rect.left;
	int win_h = win_rect.bottom - win_rect.top;

	if (buffer->fullscreen && (float)win_w / (float)win_h == 16.0f / 9.0f)
		/* Fullscreen for 16:9 resolutions */
		StretchDIBits(context,
			0, 0, win_w, win_h,			/* Drawing to window params		*/
			0, 0, buffer->w, buffer->h,	/* Drawing to from our renderer */
			buffer->memory, &buffer->info, DIB_RGB_COLORS, SRCCOPY);
	else if (buffer->fullscreen && ((float)win_w / (float)win_h != 16.0f / 9.0f))
		/* Support for non-16:9 resolutions. */
		StretchDIBits(context,
			0, 0, win_w, (int)(win_w * (9.0f / 16.0f)),	/* Drawing to window params		*/
			0, 0, buffer->w, buffer->h,					/* Drawing to from our renderer */
			buffer->memory, &buffer->info, DIB_RGB_COLORS, SRCCOPY);
	else
		/* Windowed */
		StretchDIBits(context,
			0, 0, buffer->w, buffer->h,	/* Drawing to window params		*/
			0, 0, buffer->w, buffer->h,	/* Drawing to from our renderer */
			buffer->memory, &buffer->info, DIB_RGB_COLORS, SRCCOPY);
}

/* *** SOUND *** */
#define DSOUND_CREATE(var) HRESULT WINAPI var(LPCGUID pcGui_device, LPDIRECTSOUND *p_dsound, LPUNKNOWN p_unknown)
typedef DSOUND_CREATE(dsound_create);
local void Init_DirectSound(HWND win, Win32_sound_buffer *sbuffer)
{
	HMODULE dsound_lib = LoadLibraryA("dsound.dll");

	if (dsound_lib)
	{
		dsound_create *DirectSoundCreate =
			(dsound_create *)GetProcAddress(dsound_lib, "DirectSoundCreate");
		LPDIRECTSOUND dsound;
		if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &dsound, 0)))
		{
			WAVEFORMATEX wave_format = {};
			wave_format.wFormatTag = WAVE_FORMAT_PCM;
			wave_format.nChannels = 2;
			wave_format.nSamplesPerSec = (DWORD)sbuffer->samples_per_sec;
			wave_format.wBitsPerSample = 16;
			wave_format.nBlockAlign = (DWORD)((wave_format.nChannels * wave_format.wBitsPerSample) / 8);
			wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
			wave_format.cbSize = 0;

			if (SUCCEEDED(dsound->SetCooperativeLevel(win, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC buffer_desc = {};
				buffer_desc.dwSize = sizeof(buffer_desc);
				buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
				buffer_desc.dwBufferBytes = (DWORD)sbuffer->sbuffer_size;

				LPDIRECTSOUNDBUFFER primary_sbuffer;
				if (SUCCEEDED(dsound->CreateSoundBuffer(&buffer_desc, &primary_sbuffer, 0)))
					if (SUCCEEDED(primary_sbuffer->SetFormat(&wave_format))) {}
					else { /* Failed to set format! */ }
			}
			else { /* Failed to set the Cooperative Level! */ }

			/* This is where we'll implement sound */
			DSBUFFERDESC buffer_desc = {};
			buffer_desc.dwSize = sizeof(buffer_desc);
			buffer_desc.dwFlags = DSBCAPS_GLOBALFOCUS;
			buffer_desc.dwBufferBytes = (DWORD)sbuffer->sbuffer_size;
			buffer_desc.lpwfxFormat = &wave_format;

			if (SUCCEEDED(dsound->CreateSoundBuffer(&buffer_desc, &second_sbuffer, 0))) {}
		}
		else { /* Failed to create a DirectSound object! */ }
	}
	else { /* Failed to load DirectSound! */ }
}
local void Win32_fill_sound_buffer(Win32_sound_buffer *win_sbuffer, Sound_buffer *sbuffer, DWORD byte_to_lock, DWORD bytes_to_write)
{
	VOID *region[2];
	DWORD region_size[2];
	if (SUCCEEDED(second_sbuffer->Lock(byte_to_lock, bytes_to_write, 
		&region[0], &region_size[0], &region[1], &region_size[1], 0)))
	{
		short *main_sample = sbuffer->samples;
		for (int r = 0; r < 2; r++)
		{
			DWORD region_count = region_size[r] / win_sbuffer->bytes_per_sample;
			short *sample_out = (short *)region[r];
			for (DWORD sample_index = 0; sample_index < region_count; ++sample_index)
			{
				*sample_out++ = *main_sample++;
				*sample_out++ = *main_sample++;
				++win_sbuffer->active_sample_count;
			}
		}
		second_sbuffer->Unlock(region[0], region_size[0], region[1], region_size[1]);
	}
}
local void Win32_clear_buffer(Win32_sound_buffer *sbuffer)
{
	VOID *region[2];
	DWORD region_size[2];
	if (SUCCEEDED(second_sbuffer->Lock(0, (DWORD)sbuffer->sbuffer_size, 
		&region[0], &region_size[0], &region[1], &region_size[1], 0)))
	{
		for (int r = 0; r < 2; r++)
		{
			uchar *sample_out = (uchar *)region[r];
			for (DWORD byte_index = 0; byte_index < region_size[r]; ++byte_index) { *sample_out++ = 0; }
		}
		second_sbuffer->Unlock(region[0], region_size[0], region[1], region_size[1]);
	}
}

/* *** CONTROLLER INPUT *** */
#if CONTROLLER_EX_BUILD
/*	XInput Redefinitions ; This is a workaround to access the XInput library without
	Requiring the entire library to be imported. It's rather difficult to understand atm.	*/
#define XINPUT_GET(var) DWORD WINAPI var(DWORD dwUserIndex, XINPUT_STATE *pState)
#define XINPUT_SET(var) DWORD WINAPI var(DWORD dwUserIndex, XINPUT_VIBRATION *pVibrate)
#define XInputGetState XInputGetState_
#define XInputSetState XInputSetState_

/*	We're defining types that are functions to the functions above. This lets
	us access those functions directly from 'xinput.h' using our own definitions.			 */
typedef XINPUT_GET(xinput_get);
typedef XINPUT_SET(xinput_set);

XINPUT_GET(get_stub) { return ERROR_DEVICE_NOT_CONNECTED; }
XINPUT_SET(set_stub) { return ERROR_DEVICE_NOT_CONNECTED; }

global xinput_get *XInputGetState_ = get_stub;
global xinput_set *XInputSetState_ = set_stub;

local void Win32_load_XInput()
{
	HMODULE xinput_lib = LoadLibraryA("xinput1_4.dll");
	if (!xinput_lib) xinput_lib = LoadLibraryA("xinput1_3.dll");
	if (xinput_lib)
	{
		XInputGetState = (xinput_get *)GetProcAddress(xinput_lib, "XInputGetState");
		XInputSetState = (xinput_set *)GetProcAddress(xinput_lib, "XInputSetState");
	}
	else { /* Failed to load an XINPUT library! */ }
}

local void Win32_process_XInput(DWORD XInput_button_state, DWORD button_bit, Game_Button *old_state, Game_Button *new_state)
{
	new_state->ended_down = ((XInput_button_state & button_bit) == button_bit);
	new_state->half_trans_count = (old_state->ended_down != new_state->ended_down) ? 1 : 0;
}
#endif
local void Win32_keyboard_input(Game_Button *button_state, bool key_down)
{
	if (button_state->ended_down != key_down)
	{
		button_state->ended_down = key_down;
		++button_state->half_trans_count;
	}
}

/* *** LOADING GAME DLL AND FUNCTIONS *** */
inline FILETIME Win32_get_file_time(char *source) 
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	GetFileAttributesEx(source, GetFileExInfoStandard, &data);
	return data.ftLastWriteTime;
}
local Win32_game_code Win32_load_game_code(char *source, char *source_temp)
{
	Win32_game_code game = {};

#if DEBUG_BUILD
	game.dll_last_write_time = Win32_get_file_time(source);
	CopyFile(source, source_temp, FALSE);
	game.game_lib = LoadLibraryA(source_temp);
#else
	game.game_lib = LoadLibraryA(source);
#endif

	game.update = 0, game.get_sound_samples = 0;
	if (game.game_lib)
	{
		game.update = (game_update *)GetProcAddress(game.game_lib, "Game_update");
		game.get_sound_samples = (game_get_sound_samples *)GetProcAddress(game.game_lib, "Game_get_sound_samples");
	}
	else { /* Failed to load main game library! */ }
	return game;
}
local void Win32_unload_game_code(Win32_game_code *game)
{
	if (game->game_lib)
	{
		FreeLibrary(game->game_lib);
		game->game_lib = 0;
	}
		
	else { /* Failed to find game library! */ }
	game->get_sound_samples = 0;
	game->update = 0;
}

/* *** RECORDING OPERARIONS *** */
local void Win32_get_nimr_file_location(Win32_state *state, char *dest, int slot_index, bool input_stream)
{
	char temp[64];
	StringCbPrintfA(temp, STRSAFE_MAX_CCH * sizeof(TCHAR), "Debug Recording %d_%s.nimr", slot_index, input_stream ? "input" : "state");
	Win32_get_build_exe_file_path(state, temp, dest);
}
local Win32_replay_buffer *Win32_get_replay_buffer(Win32_state *state, uint index)
{
	return &state->r_buffers[index];
}
local void Win32_start_recording(Win32_state *state, int recording_index)
{
	Win32_replay_buffer *r_buffer = Win32_get_replay_buffer(state, recording_index);

	state->recording_index = recording_index;

	char file_name[WIN32_STATE_FILE_NAME_COUNT];
	Win32_get_nimr_file_location(state, file_name, recording_index, true);

	state->recording_handle = CreateFileA(file_name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

	CopyMemory(r_buffer->mem_block, state->game_mem_block, state->total_size);
}
local void Win32_stop_recording(Win32_state *state)
{
	CloseHandle(state->recording_handle);
	state->recording_index = 0;
}
local void Win32_record_input(Win32_state *state, Input *input)
{
	DWORD written_bytes;
	WriteFile(state->recording_handle, input, (uint)sizeof(*input), &written_bytes, 0);
}

local void Win32_start_playback(Win32_state *state, int playback_index)
{
	Win32_replay_buffer *r_buffer = Win32_get_replay_buffer(state, playback_index);
	state->playback_index = playback_index;

	char file_name[WIN32_STATE_FILE_NAME_COUNT];
	Win32_get_nimr_file_location(state, file_name, playback_index, true);

	state->playback_handle = CreateFileA(file_name, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);

	CopyMemory(state->game_mem_block, r_buffer->mem_block, state->total_size);
}
local void Win32_stop_playback(Win32_state *state)
{
	CloseHandle(state->playback_handle);
	state->playback_index = 0;
}
local void Win32_playback_input(Win32_state *state, Input *input)
{
	DWORD read_bytes = 0;
	if(ReadFile(state->playback_handle, input, (DWORD)sizeof(*input), &read_bytes, 0))
		if (read_bytes == 0)
		{
			int play_index = state->playback_index;
			Win32_stop_playback(state);
			Win32_start_playback(state, play_index);
			ReadFile(state->playback_handle, input, (DWORD)sizeof(*input), &read_bytes, 0);
		}
}

/* *** MAIN WINDOWS OPERATIONS *** */
LRESULT CALLBACK WindowProc(HWND win, UINT msg, WPARAM w_param, LPARAM l_param)
{
	switch (msg)
	{
	case WM_CLOSE: { running = false; } break;		/* Confirm with user to close window?					*/
	case WM_DESTROY: { running = false; } break;	/* Handle as error; recreate window						*/
	case WM_SIZE: {} break;							/* Has the window been resized?							*/
	case WM_ACTIVATEAPP: {} break;					/* Is the window active on the screen?					*/
	case WM_SETCURSOR: { SetCursor(0); } break;		/* When set to '0', the cursor is hidden from display.	*/
	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC context = BeginPaint(win, &paint);
		Win32_update_win(&win32_buffer, win, context);
		EndPaint(win, &paint);
	} break;
	default: { return DefWindowProcA(win, msg, w_param, l_param); }
	}
	return 0;
};

local void Win32_process_key_input(Win32_state *state, Controller *controller)
{
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		switch (msg.message)
		{
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				uint VKCODE = (uint)msg.wParam;
				bool key_was_down = (msg.lParam & (1 << 30)) != 0;
				bool key_down = (msg.lParam & (1 << 31)) == 0;
				if (key_was_down != key_down)
				{
					switch (VKCODE)
					{
					case 'W':
					{
						OutputDebugStringA("W is pressed.\n");
						Win32_keyboard_input(&controller->up, key_down);
					} break;
					case 'A':
					{
						OutputDebugStringA("A is pressed.\n");
						Win32_keyboard_input(&controller->left, key_down);
					} break;
					case 'S':
					{
						OutputDebugStringA("S is pressed.\n");
						Win32_keyboard_input(&controller->down, key_down);
					} break;
					case 'D':
					{
						OutputDebugStringA("D is pressed.\n");
						Win32_keyboard_input(&controller->right, key_down);
					} break;
					case 'Q':
					{
						OutputDebugStringA("Q is pressed.\n");
					} break;
					case 'E':
					{
						OutputDebugStringA("E is pressed.\n");
						Win32_keyboard_input(&controller->action, key_down);
					} break;
					case VK_F10:
					{
						Win32_keyboard_input(&controller->fullscreen, key_down);
					} break;
					case VK_F11:
					{
						Win32_keyboard_input(&controller->ff, key_down);
					} break;
					case VK_SPACE: { OutputDebugStringA("SPACE is pressed.\n"); } break;
					case VK_UP: 
					{ 
						OutputDebugStringA("UP is pressed.\n"); 
						Win32_keyboard_input(&controller->a_up, key_down);
					} break;
					case VK_DOWN: 
					{
						OutputDebugStringA("DOWN is pressed.\n");
						Win32_keyboard_input(&controller->a_down, key_down);
					} break;
					case VK_LEFT: 
					{
						OutputDebugStringA("LEFT is pressed.\n");
						Win32_keyboard_input(&controller->a_left, key_down);
					} break;
					case VK_RIGHT: 
					{
						OutputDebugStringA("RIGHT is pressed.\n");
						Win32_keyboard_input(&controller->a_right, key_down);
					} break;
					case VK_ESCAPE: { running = false; } break;
#if DEBUG_BUILD
					case 'L':
					{
						if (key_down)
						{
							if (state->playback_index == 0)
							{
								if (state->recording_index == 0)
									Win32_start_recording(state, 1);
								else
								{
									Win32_stop_recording(state);
									Win32_start_playback(state, 1);
								}
							}
							else { Win32_stop_playback(state); }
						}
					}
#endif
						/* TO DO: Try to implement ALT+F4 properly. */
					}

					bool alt_down = msg.lParam & (1 << 29);
					if ((VKCODE == VK_F4) && alt_down) { running = false; }
				}
			}
			default:
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}
}

inline LARGE_INTEGER Win32_get_wall_clock()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return counter;
}
inline float Win32_get_sec_elapsed(LARGE_INTEGER start, LARGE_INTEGER end)
{ return (float)(end.QuadPart - start.QuadPart) / (float)frequency; }

local void Win32_get_exe_file_name(Win32_state *state)
{
	GetModuleFileNameA(0, state->exe_name, sizeof(state->exe_name));
	state->last_slash_plus_one = state->exe_name;
	
	for (char *scan = state->exe_name; *scan; ++scan)
		if (*scan == '\\') { state->last_slash_plus_one = scan + 1; }
}

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int show_code)
{
	WNDCLASSA win_class = {};	/* This is the struct that creates the window. */
	win_class.style = CS_HREDRAW | CS_VREDRAW;
	win_class.lpfnWndProc = WindowProc;
	win_class.hInstance = instance;
	win_class.hIcon = 0;
	win_class.hCursor = 0;
	win_class.hbrBackground = 0;
	win_class.lpszMenuName = 0;
	win_class.lpszClassName = "Game Engine Class";

	int default_w = 960, default_h = 540;
	Win32_resize_DIB_sect(&win32_buffer, default_w, default_h);

	Win32_state game_state = {};
	Win32_get_exe_file_name(&game_state);

	char *game_dll_name = "engine.dll";
	char game_dll_path[WIN32_STATE_FILE_NAME_COUNT];
	Win32_get_build_exe_file_path(&game_state, game_dll_name, game_dll_path);

	char *game_dll_name_temp = "engine_temp.dll";
	char game_dll_path_temp[WIN32_STATE_FILE_NAME_COUNT];
	Win32_get_build_exe_file_path(&game_state, game_dll_name_temp, game_dll_path_temp);

#if CONTROLLER_EX_BUILD
	/* This allows us to accept controller input from a compatible controller or keyboard. */
	Win32_load_XInput(); 
#endif

	if (RegisterClass(&win_class))
	{
		HWND win_handle = CreateWindowEx(
			0,
			win_class.lpszClassName,
			"Game Engine",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			400, 200,				/* x-pos @ start, y-pos @ start */
			default_w, default_h,	/* Window resolution			*/
			0, 0,					/* hWndParent, hMenu			*/
			instance,
			0);

		if (win_handle)
		{
			bool sleep_toggled = (timeBeginPeriod(1) == TIMERR_NOERROR); /* This sets the Window's Sleep() function to refresh every millisecond. */
			HDC context = GetDC(win_handle);

			Pixel_buffer local_buffer = {
				local_buffer.fullscreen = false,
				local_buffer.memory = win32_buffer.memory,
				local_buffer.w = win32_buffer.w,
				local_buffer.h = win32_buffer.h,
				local_buffer.pitch = win32_buffer.pitch
			};

			win32_buffer.fullscreen = false;

			Win32_game_code game = Win32_load_game_code(game_dll_path, game_dll_path_temp);

			Init_DirectSound(win_handle, &win32_sbuffer);
			Win32_clear_buffer(&win32_sbuffer);
			second_sbuffer->Play(0, 0, DSBPLAY_LOOPING);
			short *samples = (short *)VirtualAlloc(0, (size_t)win32_sbuffer.sbuffer_size,
				MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

			/* *** MEMORY ALLOCATION AND PERFORMANCE TRACKING *** */

#if DEBUG_BUILD
			LPVOID base_address = (LPVOID)GB(2);
#else		
			LPVOID base_address = 0;
#endif
			Game_memory game_mem = {};
			game_mem.perma_storage_size = MB(64);
			game_mem.adaptive_storage_size = MB(256);

			game_mem.D_platform_free_file_mem = D_platform_free_file_mem;
			game_mem.D_platform_read = D_platform_read;
			game_mem.D_platform_write = D_platform_write;

			game_state.total_size = game_mem.perma_storage_size + game_mem.adaptive_storage_size;
			game_state.game_mem_block = VirtualAlloc(base_address, (size_t)game_state.total_size,
				MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			game_mem.perma_storage = game_state.game_mem_block;
			game_mem.adaptive_storage = (char *)game_mem.perma_storage + game_mem.perma_storage_size;

#if DEBUG_BUILD
			for (int replay_index = 0; replay_index < ARRAY_COUNT(game_state.r_buffers); replay_index++)
			{
				Win32_replay_buffer *r_buffer = &game_state.r_buffers[replay_index];

				Win32_get_nimr_file_location(&game_state, r_buffer->replay_name, replay_index, false);
				r_buffer->file_handle = CreateFile(r_buffer->replay_name, GENERIC_WRITE|GENERIC_READ, 0, 0, CREATE_ALWAYS, 0, 0);

				LARGE_INTEGER max_size;
				max_size.QuadPart = game_state.total_size;
				r_buffer->mem_map = CreateFileMapping(r_buffer->file_handle, 0, PAGE_READWRITE,
					max_size.HighPart, max_size.LowPart, 0);

				r_buffer->mem_block = MapViewOfFile(r_buffer->mem_map, FILE_MAP_ALL_ACCESS, 0, 0, game_state.total_size);
				if (r_buffer->mem_block) {}
			}
#endif
			LARGE_INTEGER frequency_result;
			QueryPerformanceFrequency(&frequency_result);
			frequency = frequency_result.QuadPart;

			if (samples && game_mem.perma_storage && game_mem.adaptive_storage)
			{
				/*	QueryPerformance Counter and RDTSC -- Operations for displaying
						performance of the program.										*/
				LARGE_INTEGER start_counter = Win32_get_wall_clock();
				LARGE_INTEGER flip_clock = Win32_get_wall_clock();

				int monitor_refresh_rate = 60;
				int win32_refresh_data = GetDeviceCaps(context, VREFRESH); /* Computing of actual FPS is subject to change. */
				if (win32_refresh_data > 1) monitor_refresh_rate = win32_refresh_data;
				float game_update_refresh = monitor_refresh_rate / 2.0f;

				float target_sec_per_frame = 1.0f / game_update_refresh;
				
				win32_sbuffer.safety_bytes = (int)(((float)win32_sbuffer.samples_per_sec * (float)win32_sbuffer.bytes_per_sample / game_update_refresh) / 2.0f);
				DWORD audio_latency_bytes = 0;
				float audio_latency_sec = 0;
				bool sbuffer_success = false;

				Input input[2] = {}; /* Key input */
				Input *new_input = &input[0];
				Input *old_input = &input[1];

				while (running) /* Window Loop; if running is false, window closes. */
				{
					game_state.time_speed = target_sec_per_frame;
					new_input->time_speed = game_state.time_speed;
#if DEBUG_BUILD
					FILETIME new_dll_write_time = Win32_get_file_time(game_dll_name);
					if (CompareFileTime(&new_dll_write_time, &game.dll_last_write_time) != 0)
					{
						Win32_unload_game_code(&game);
						game = Win32_load_game_code(game_dll_path, game_dll_path_temp);
					}
#endif
					/*  *** KEYBOARD/CONTROLLER OPERATIONS ***  */

					Controller *keyboard_controller_old = Get_controller(old_input, 0);
					Controller *keyboard_controller_new = Get_controller(new_input, 0);
					Controller null_controller = {};
					*keyboard_controller_new = null_controller;
					keyboard_controller_new->connected = true;

					for (int button_index = 0; button_index < ARRAY_COUNT(keyboard_controller_new->buttons); ++button_index)
						keyboard_controller_new->buttons[button_index].ended_down =
							keyboard_controller_old->buttons[button_index].ended_down;

					Win32_process_key_input(&game_state, keyboard_controller_new);

					POINT mouse_p;
					GetCursorPos(&mouse_p);
					ScreenToClient(win_handle, &mouse_p);
					new_input->mouse_x = mouse_p.x, new_input->mouse_y = mouse_p.y, new_input->mouse_z = 0;
					Win32_keyboard_input(&new_input->mouse_buttons[0], GetKeyState(VK_LBUTTON) & (1 << 15));
					Win32_keyboard_input(&new_input->mouse_buttons[1], GetKeyState(VK_RBUTTON) & (1 << 15));
					Win32_keyboard_input(&new_input->mouse_buttons[2], GetKeyState(VK_MBUTTON) & (1 << 15));

					if (keyboard_controller_new->buttons[8].ended_down && !keyboard_controller_old->buttons[8].ended_down)
						if (GetWindowLongPtr(win_handle, GWL_STYLE) & WS_POPUP)
						{
							win32_buffer.fullscreen = false;

							SetWindowLongPtr(win_handle, GWL_STYLE, WS_VISIBLE | WS_OVERLAPPEDWINDOW);
							SetWindowPos(win_handle, NULL, 400, 200, default_w, default_h, SWP_FRAMECHANGED);
						}
						else
						{
							win32_buffer.fullscreen = true;
							int screen_w = GetSystemMetrics(SM_CXSCREEN), screen_h = GetSystemMetrics(SM_CYSCREEN);
							SetWindowLongPtr(win_handle, GWL_STYLE, WS_VISIBLE | WS_POPUP);
							SetWindowPos(win_handle, HWND_TOP, 0, 0, screen_w, screen_h, SWP_FRAMECHANGED);
						}
#if DEBUG_EX
					char buffer[256];
					StringCbPrintfA(buffer, STRSAFE_MAX_CCH * sizeof(TCHAR), "Mouse X: %u\t|\tMouse Y: %u\n", mouse_p.x, mouse_p.y);
					OutputDebugString(buffer);
#endif

#if CONTROLLER_EX_BUILD
					/* XINPUT Loop : Get the number of controllers available and their current states. */
					DWORD max_controllers = (XUSER_MAX_COUNT > ARRAY_COUNT(new_input->controller) - 1) ?
						ARRAY_COUNT(new_input->controller) - 1 : XUSER_MAX_COUNT;
					for (DWORD controller_index = 0; controller_index < max_controllers; controller_index++)
					{
						/* We start at [index + 1] because controller[0] is the keyboard. */
						Controller *old_controller = Get_controller(old_input, controller_index + 1);
						Controller *new_controller = Get_controller(new_input, controller_index + 1);

						XINPUT_STATE controller_state;
						if ((XInputGetState_(controller_index, &controller_state)) == ERROR_SUCCESS)
						{
							new_controller->connected = true;
							new_controller->is_analog = old_controller->is_analog;
							XINPUT_GAMEPAD *gamepad = &controller_state.Gamepad;

							new_controller->avg_x = (gamepad->sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ?
								(float)gamepad->sThumbLX / 32768.0f : (gamepad->sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ?
								(float)gamepad->sThumbLX / 32767.0f : 0;
							new_controller->avg_y = (gamepad->sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ?
								(float)gamepad->sThumbLY / 32768.0f : (gamepad->sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ?
								(float)gamepad->sThumbLY / 32767.0f : 0;

							if (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_UP)
								new_controller->avg_y = 1.0f;

							if (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
								new_controller->avg_y = -1.0f;

							if (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
								new_controller->avg_x = -1.0f;

							if (gamepad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
								new_controller->avg_x = 1.0f;

							if (new_controller->avg_x == 1.0f || new_controller->avg_x == -1.0f ||
								new_controller->avg_y == 1.0f || new_controller->avg_y == -1.0f)
								new_controller->is_analog = false;

							float move_threshold = 0.5f;
							Win32_process_XInput((DWORD)((new_controller->avg_y > move_threshold) ? 1 : 0), 1,
								&old_controller->up, &new_controller->up);
							Win32_process_XInput((DWORD)((new_controller->avg_y < -move_threshold) ? 1 : 0), 1,
								&old_controller->down, &new_controller->down);
							Win32_process_XInput((DWORD)((new_controller->avg_x < move_threshold) ? 1 : 0), 1,
								&old_controller->left, &new_controller->right);
							Win32_process_XInput((DWORD)((new_controller->avg_x < -move_threshold) ? 1 : 0), 1,
								&old_controller->right, &new_controller->left);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_A, &old_controller->action,
								&new_controller->a_up);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_B, &old_controller->action,
								&new_controller->a_down);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_X, &old_controller->action,
								&new_controller->a_left);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_Y, &old_controller->action,
								&new_controller->a_right);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_START, &old_controller->action,
								&new_controller->action);
							Win32_process_XInput(gamepad->wButtons, XINPUT_GAMEPAD_BACK, &old_controller->action,
								&new_controller->back);
						}
						else { new_controller->connected = false; }
					}
#endif
					Threading threads = {};
#if DEBUG_BUILD
					if (game_state.recording_index)
						Win32_record_input(&game_state, new_input);
					if (game_state.playback_index)
						Win32_playback_input(&game_state, new_input);
#endif
					
					if (game.update) game.update(&threads, &game_mem, &local_buffer, new_input);

					/*	*** SOUND HANDLING ***	*/

					DWORD play_cursor, write_cursor;
					DWORD target_cursor = 0, byte_to_lock = 0, bytes_to_write = 0;

					LARGE_INTEGER audio_clock = Win32_get_wall_clock();
					float time_until_audio = Win32_get_sec_elapsed(flip_clock, audio_clock);
					if (second_sbuffer->GetCurrentPosition(&play_cursor, &write_cursor) == DS_OK)
					{
						if (!sbuffer_success)
						{
							win32_sbuffer.active_sample_count = write_cursor / win32_sbuffer.bytes_per_sample;
							sbuffer_success = true;
						}
						/* Stupidly-complicated stuff. I'll try to figure it out one of these days. */
						DWORD expected_bytes_per_frame = (int)((float)(win32_sbuffer.samples_per_sec * win32_sbuffer.bytes_per_sample) / game_update_refresh);
						float seconds_until_flip = target_sec_per_frame - time_until_audio;
						DWORD expected_bytes_per_flip = (DWORD)(seconds_until_flip / target_sec_per_frame) * expected_bytes_per_frame;
						DWORD expected_frame_boundary = play_cursor + expected_bytes_per_flip;

						DWORD safe_write_cursor = ((write_cursor < play_cursor) ?
							write_cursor + win32_sbuffer.sbuffer_size : write_cursor)
							+ win32_sbuffer.safety_bytes;

						bool latent_audio = (safe_write_cursor >= expected_frame_boundary);

						target_cursor = latent_audio ?
							(write_cursor + expected_bytes_per_frame + win32_sbuffer.safety_bytes) % win32_sbuffer.sbuffer_size :
							(expected_frame_boundary + expected_bytes_per_frame) % win32_sbuffer.sbuffer_size;
						byte_to_lock = (win32_sbuffer.active_sample_count * win32_sbuffer.bytes_per_sample) % win32_sbuffer.sbuffer_size;
						bytes_to_write = (byte_to_lock > target_cursor) ?
							(win32_sbuffer.sbuffer_size - byte_to_lock) + target_cursor : target_cursor - byte_to_lock;

						Sound_buffer main_sbuffer = {};
						main_sbuffer.samples_per_sec = win32_sbuffer.samples_per_sec;
						main_sbuffer.sample_count = (int)(bytes_to_write / win32_sbuffer.bytes_per_sample);
						main_sbuffer.samples = samples;
						if (game.get_sound_samples) game.get_sound_samples(&threads, &game_mem, &main_sbuffer);

						Win32_fill_sound_buffer(&win32_sbuffer, &main_sbuffer, byte_to_lock, bytes_to_write);
					}
					else { sbuffer_success = false; }

#if DEBUG_EX
					/* TESTING: Displays audio latency and write/play positions. */
					if (sbuffer_success)
					{
						second_sbuffer->GetCurrentPosition(&play_cursor, &write_cursor);
						audio_latency_bytes = (write_cursor < play_cursor) ?
							(write_cursor + win32_sbuffer.sbuffer_size) - play_cursor : write_cursor - play_cursor;
						audio_latency_sec = ((float)audio_latency_bytes / (float)win32_sbuffer.bytes_per_sample) / (float)win32_sbuffer.samples_per_sec;

						if (sbuffer_success)
						{
							char buffer_s[256];
							StringCbPrintfA(buffer_s, STRSAFE_MAX_CCH * sizeof(TCHAR), "play: %u byte lock: %u target: %u byte write: %u\n ALS: %f\n",
								play_cursor, byte_to_lock, target_cursor, bytes_to_write, audio_latency_sec);
							OutputDebugString(buffer_s);
						}
					}
#endif
					/*  *** FRAMERATE CHECKING ***  */

					LARGE_INTEGER end_counter = Win32_get_wall_clock();
					float cycle_duration = Win32_get_sec_elapsed(start_counter, end_counter); /* Time elapsed in this cycle. */
					if (cycle_duration < target_sec_per_frame)
					{
						if (sleep_toggled)
						{
							DWORD sleep_ms = (DWORD)(1000.0f * (target_sec_per_frame - cycle_duration));
							if (sleep_ms > 0)
								Sleep(sleep_ms);
						}
						while (cycle_duration < target_sec_per_frame)
							cycle_duration = Win32_get_sec_elapsed(start_counter, Win32_get_wall_clock()); /* Make sure that the cycle stops here. */
					}	
					else { /* Frame skipped */ }
					LARGE_INTEGER final_counter = Win32_get_wall_clock();
					float total_ms = 1000.0f * Win32_get_sec_elapsed(start_counter, final_counter); /* Time in ms */
					start_counter = final_counter;

/* TESTING: Displays MS between frames and FPS */
#if DEBUG_EX
					double fps = 0; // (double)(frequency / (total_ms * 1000));  // <- Needs recalculating
					char buffer_f[256];
					StringCbPrintfA(buffer_f, STRSAFE_MAX_CCH * sizeof(TCHAR), "%.02f ms/f %.02ffps\n", total_ms, fps);
					OutputDebugString(buffer_f);
#endif
					/*  *** SCREEN RENDERING ***  */

					Win32_update_win(&win32_buffer, win_handle, context);

					flip_clock = Win32_get_wall_clock();

					Input *temp = new_input;
					new_input = old_input;
					old_input = temp;
				}
				ReleaseDC(win_handle, context);
			}
		}
		else { /* Window handling failed! */ }
	}
	else { /* Failed to register the window class! */ }

	return EXIT_SUCCESS;
}