#if !defined(WIN32_ENGINE)
/* For rendering purposes. */
struct Win32_pixel_buffer
{
	bool fullscreen;
	BITMAPINFO info;
	void *memory;
	int w, h;
	int pitch;
	int bpp = 4;	/* Byte Per Pixel */
} win32_buffer;
/* For sound output. */
struct Win32_sound_buffer
{
	int samples_per_sec = 48000;
	int bytes_per_sample = sizeof(short) * 2;
	int sbuffer_size = samples_per_sec * bytes_per_sample;
	uint active_sample_count;
	DWORD safety_bytes;
} win32_sbuffer;
/* Interacts with the game's DLL functions. */
struct Win32_game_code
{
	HMODULE game_lib;
	FILETIME dll_last_write_time;
	/*	Any of these type-defined functions must be checked before calling.
		Example: if (func) func;											*/
	game_update *update;
	game_get_sound_samples *get_sound_samples;
};
/* Used for looped code editing. There's a file handle for the recording, and one for the playback. */
#define WIN32_STATE_FILE_NAME_COUNT MAX_PATH
struct Win32_replay_buffer
{
	HANDLE file_handle;
	HANDLE mem_map;
	void *mem_block;
	char replay_name[WIN32_STATE_FILE_NAME_COUNT];
};
struct Win32_state
{
	void *game_mem_block;
	uint total_size;
	Win32_replay_buffer r_buffers[4];

	char exe_name[WIN32_STATE_FILE_NAME_COUNT]; /* Don't use MAX_PATH regularly. */
	char *last_slash_plus_one; /* Used to get the exact location of this file. */

	HANDLE recording_handle;
	int recording_index;
	HANDLE playback_handle;
	int playback_index;

	float time_speed;
};
#define WIN32_ENGINE
#endif