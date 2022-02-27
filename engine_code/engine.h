#if !defined(ENGINE_H)

/*	We're redefining static for organizational purposes.
	'local' variables are subject to be removed later if used.	 */
#define local static	/* Accessible within the file it's called in.	*/
#define global static	/* Accessible from all associated files.		*/

/* This should be used to kill the program loop */
global bool running = 1;

/* Space allocation macros */
#define KB(val) (val * 1024LL)
#define MB(val) (KB(val) * 1024LL)
#define GB(val) (MB(val) * 1024LL)

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
typedef unsigned char uchar;
typedef int unsigned uint;


/* Finding which compiler we're using. This will determine the insintrics we can use at runtime. */
#if !defined(COMPILER_MSVC)
#define COMPILER_MSVC 0
#endif
#if !defined(COMPILER_LLVM)
#define COMPILER_LLVM 0
#endif

#if !COMPILER_MSVC && !COMPILER_LLVM
	#if _MSC_VER
	#undef COMPILER_MSVC
	#define COMPILER_MSVC 1

	#else
	#undef COMPILER_LLVM
	#define COMPILER_LLVM 1
	#endif
#endif

#if COMPILER_MSVC

#endif

#include "graphics.h"

/* Services the platform layer provides to the game. */

struct Threading
{
	int x;
};

struct D_read_result
{
	long long size;
	void *content;
};
#define D_PLATFORM_FREE_FILE_MEM(func) void func(Threading *threading, void *memory)
typedef D_PLATFORM_FREE_FILE_MEM(d_platform_free_file_mem);

#define D_PLATFORM_READ(func) D_read_result func(Threading *threading, char *file_name)
typedef D_PLATFORM_READ(d_platform_read);

#define D_PLATFORM_WRITE(func) bool func(Threading *threading, char *file_name, D_read_result *file)
typedef D_PLATFORM_WRITE(d_platform_write);

/* Services the game provides to the platform layer. */
/* audio aud_var, bit_buffer render_var, input input_var, timing fixed_fps */

struct Game_memory
{
	bool initialized;
	uint perma_storage_size;
	void *perma_storage;
	uint adaptive_storage_size;
	void *adaptive_storage;

	d_platform_read *D_platform_read;
	d_platform_write *D_platform_write;
	d_platform_free_file_mem *D_platform_free_file_mem;
};

struct Pixel_buffer
{
	bool fullscreen;
	void *memory;
	int w, h;
	int pitch;
	int bpp = 4;	/* Byte Per Pixel */
};

struct Sound_buffer
{
	int sample_count;
	int samples_per_sec;
	short *samples;
};

struct Game_Button
{
	int half_trans_count;
	bool ended_down;
};
struct Controller
{
	float avg_x, avg_y;
	bool is_analog;
	bool connected;

	union
	{
		Game_Button buttons[11];
		struct
		{
			Game_Button up;
			Game_Button down;
			Game_Button left;
			Game_Button right;
			/* Action controls prefixed with 'a_' */
			Game_Button a_up;
			Game_Button a_down;
			Game_Button a_left;
			Game_Button a_right;

			Game_Button fullscreen;
			Game_Button action;
			Game_Button back;
			Game_Button ff;
		};
	};
};
struct Input
{
	float time_speed;
	Game_Button mouse_buttons[3];
	int mouse_x, mouse_y, mouse_z;
	Point mouse_pos;
	Controller controller[5];
	float seconds_elapsed;
};
inline Controller *Get_controller(Input *input, int controller_index)
{
	return &input->controller[controller_index];
}

/* Game Properties */
struct World
{
	float game_unit, unit_to_pixels;
	int pixel_unit;
	Box menu;
	int menu_state;
	bool menu_closed;
	float menu_force;
};
struct Bitmap
{
	uint *image;
	int w, h;
};
struct Character {
	Point location;
	Point vel;
	Rect sprite;
	Point origin;
};
struct Game_state
{
	Character player;
	Character npc;
	bool npc_move;
	Point marked_location;
	Point destination;
	Point distance;
	float vel, accel, deadzone;
	bool sliding, left, right;
	Color color;
	int color_state;
	float cooldown;

	Point cursor;
	Bitmap cursor_img;

	/* A multiplicative element that scales time speed. */
	float time_accel;
	/* The speed of time. */
	float time_speed;

	Bitmap bmp;
	World world;
};

/* Define a function that creates a new function with the desired parameters. */
#define GAME_UPDATE(func) void func(Threading *threading, Game_memory *memory, Pixel_buffer *buffer, Input *input)
/* Define a type that creates this kind of function */
typedef GAME_UPDATE(game_update);

#define GAME_GET_SOUND_SAMPLES(func) void func(Threading *threading, Game_memory *memory, Sound_buffer *sbuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

#define ENGINE_H
#endif