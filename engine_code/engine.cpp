#include "engine.h"

/* 	CUSTOM GAME ENGINE by Jonavinne Steadham
	
	This engine is capable of rendering images and rectangles, 
	playing audio, and acting basic input. Additional controls are
	available in the README.txt


	*** TO-DO LIST ***

	1. Precise transitioning (Point a, Point b)
		a. Make transition smooth
		b. Adjustable easing and speed
	2. Camera space
		a. Everything moves based on the camera's location, like an offset
	3. Transformations
		a. Scale
		b. Rotation
	3. Displayable text
		a. Debug info
		b. In-game font
	4. Implement sound
		a. Character dialogue has blips of varying pitch per character of speech
		b. Ability to import music
	5. Game art & sprites (will be a long process)


	Last updated : March 7th, 2021
*/


/* Generates a square region at the given position pos */
local Rect Create_square(Point pos, float size)
{
	return { pos, { pos.x + size, pos.y + size } };
}

/* Detects whether or not the Point pos is inside the Rect region */
local bool Is_inside(Point pos, Rect region)
{
	bool in_x = false, in_y = false;
	float x_dif = (region.p2.x - region.p1.x), y_dif = (region.p2.y - region.p1.y);
	if (x_dif >= (region.p2.x - pos.x) && -x_dif <= (region.p1.x - pos.x)) 
		in_x = true;
	if (y_dif >= (region.p2.y - pos.y) && -y_dif <= (region.p1.y - pos.y)) 
		in_y = true;

	return in_x && in_y;
}

/* Draws a solid rectangle to the screen using the provided rect region. */
local void Draw_fill_rect(Pixel_buffer *buffer, Rect rect, Color color)
{
	int min_x = rect.p1.x > 0 ? Round_float(rect.p1.x) : 0;
	int min_y = rect.p1.y > 0 ? Round_float(rect.p1.y) : 0;
	int max_x = rect.p2.x < buffer->w ? Round_float(rect.p2.x) : buffer->w;
	int max_y = rect.p2.y < buffer->h ? Round_float(rect.p2.y) : buffer->h;

	uchar *row = ((uchar *)buffer->memory + min_x * buffer->bpp + min_y * buffer->pitch);
	for (int y = min_y; y < max_y; ++y)
	{
		uint *pixel = (uint *)row;
		for (int x = min_x; x < max_x; ++x)
			*pixel++ = Round_float(color.r * 255.0f) << 16 | Round_float(color.g * 255.0f) << 8 | Round_float(color.b * 255.0f);
		row += buffer->pitch;
	}
}

/* Draws an unfilled rectangle to the screen using the provided rect region and line thickness */
local void Draw_line_rect(Pixel_buffer *buffer, Rect rect, Color color, int thickness)
{
	for (int i = 0; i < thickness; ++i)
	{
		int min_x = rect.p1.x - i > 0 ? Round_float(rect.p1.x) - i : 0;
		int min_y = rect.p1.y - i > 0 ? Round_float(rect.p1.y) - i : 0;
		int max_x = rect.p2.x + i < buffer->w ? Round_float(rect.p2.x) + i - 1 : buffer->w;
		int max_y = rect.p2.y + i < buffer->h ? Round_float(rect.p2.y) + i : buffer->h;

		uchar *row = ((uchar *)buffer->memory + (min_x - 1) * buffer->bpp + min_y * buffer->pitch);
		for (int y = min_y; y < max_y; ++y)
		{
			uint *pixel = (uint *)row;
			if (y - 1 >= 0)
				for (int x = min_x; x < max_x; ++x)
				{
					if ((y > min_y && y < max_y - 1) && x > min_x)
					{
						x = max_x - 1;
						pixel += x - min_x;
					}
					else { pixel++; }
					if (x - 1 >= 0)
						*pixel = Round_float(color.r * 255.0f) << 16 | Round_float(color.g * 255.0f) << 8 | Round_float(color.b * 255.0f);
				}
			row += buffer->pitch;
		}
	}
}

/* Draws a rectangle to the screen with a colored outline. The inside and outline can have differing colors */
local void Draw_outlined_rect(Pixel_buffer *buffer, Rect rect, Color fill_color, Color line_color, int thickness)
{
	Draw_line_rect(buffer, rect, line_color, thickness);
	Draw_fill_rect(buffer, rect, fill_color);
}

#pragma pack(push, 1)
struct Bitmap_header
{
	unsigned short file_type;
	uint file_size;
	unsigned short reserved_1, reserved_2;
	uint bitmap_offset, size;
	int w, h;
	unsigned short planes, bpp;
};
#pragma pack(pop)

/* Loads a bitmap into the D_platform_read object */
local Bitmap D_Load_bmp_image(Threading *thread, d_platform_read *D_platform_read, char *file_name)
{
	Bitmap bmp = {};
	D_read_result file = D_platform_read(thread, file_name);
	if (file.size != 0)
	{
		Bitmap_header *bmp_file = (Bitmap_header *)file.content;
		bmp.image = (uint *)((uchar *)file.content + bmp_file->bitmap_offset);
		bmp.h = bmp_file->h, bmp.w = bmp_file->w;
	}
	return bmp;
}

/* Draws bmp to the screen. ONLY SUPPORTS CRLF BITMAPS */
local void Draw_bitmap(Pixel_buffer *buffer, Point origin, Bitmap bmp)
{
	int min_x = origin.x > 0 ? Round_float(origin.x) : 0;
	int min_y = origin.y > 0 ? Round_float(origin.y) : 0;
	int max_x = ((float)bmp.w + origin.x > buffer->w) ? buffer->w : Round_float((float)bmp.w + origin.x);
	int max_y = ((float)bmp.h + origin.y > buffer->h) ? buffer->h : Round_float((float)bmp.h + origin.y);

	Point offset = {
		(min_x == 0) ? (float)(bmp.w - max_x) : 0,
		(min_y == 0) ? (float)(bmp.h - max_y) : 0
	};

	uint *source_row = bmp.image + bmp.w * (bmp.h - 1);
	uchar *dest_row = ((uchar *)buffer->memory + min_x * buffer->bpp + min_y * buffer->pitch);

	source_row -= (bmp.w * (int)offset.y);

	for (int y = min_y; y < max_y; ++y)
	{
		uint *source = source_row;
		uint *pixel = (uint *)dest_row;

		source += (int)offset.x;

		for (int x = min_x; x < max_x; ++x)
		{
			if (*source != 0)
				if (*source >> 24 != 0xFF)
				{
					if (*source << 8 != 0)
					{
						/* Anti-aliasing. Very slow! */
						float a = (*source >> 24) / 255.0f;
						float b = (1.0f - a) * (uchar)(*pixel >> 16) + (a * (uchar)(*source >> 16));
						float g = (1.0f - a) * (uchar)(*pixel >> 8) + (a * (uchar)(*source >> 8));
						float r = (1.0f - a) * (uchar)*pixel + (a * (uchar)*source);
						*pixel = (*source >> 24) << 24 | Round_float(b) << 16 | Round_float(g) << 8 | Round_float(r);
					}
				}
				else { *pixel = *source; }
			*pixel++, *source++;
		}
		dest_row += buffer->pitch;
		source_row -= bmp.w;
	}
}

/* Intended to be used for rotating bitmaps. Currently unsupported. */
local void Draw_bitmap_rotation(Pixel_buffer *buffer, Point origin, Bitmap bmp, float rotation)
{
	int min_x = origin.x > 0 ? Round_float(origin.x) : 0;
	int min_y = origin.y > 0 ? Round_float(origin.y) : 0;
	int max_x = ((float)bmp.w + origin.x > buffer->w) ? buffer->w : Round_float((float)bmp.w + origin.x);
	int max_y = ((float)bmp.h + origin.y > buffer->h) ? buffer->h : Round_float((float)bmp.h + origin.y);

	Point offset = {
		(min_x == 0) ? (float)(bmp.w - max_x) : 0,
		(min_y == 0) ? (float)(bmp.h - max_y) : 0
	};

	uint *source_row = bmp.image + bmp.w * (bmp.h - 1);
	uchar *dest_row = ((uchar *)buffer->memory + min_x * buffer->bpp + min_y * buffer->pitch);

	source_row -= (bmp.w * (int)offset.y);

	for (int y = min_y; y < max_y; ++y)
	{
		uint *source = source_row;
		uint *pixel = (uint *)dest_row;

		source += (int)offset.x;

		for (int x = min_x; x < max_x; ++x)
		{
			if (*source != 0)
				if (*source >> 24 != 0xFF)
				{
					if (*source << 8 != 0)
					{
						/* Anti-aliasing. Very slow! */
						float a = (*source >> 24) / 255.0f;
						float b = (1.0f - a) * (uchar)(*pixel >> 16) + (a * (uchar)(*source >> 16));
						float g = (1.0f - a) * (uchar)(*pixel >> 8) + (a * (uchar)(*source >> 8));
						float r = (1.0f - a) * (uchar)*pixel + (a * (uchar)*source);
						*pixel = (*source >> 24) << 24 | Round_float(b) << 16 | Round_float(g) << 8 | Round_float(r);
					}
				}
				else { *pixel = *source; }
			*pixel++, *source++;
		}
		dest_row += buffer->pitch;
		source_row -= bmp.w;
	}
}
/* Returns center of window based on window resolution */
inline Point Get_window_center(Pixel_buffer buffer)
{
	return { ((float)buffer.w / 2.0f), ((float)buffer.h / 2.0f) };
}

/* Brightens the color of the box passed in */
local void Highlight_box(Box *box)
{
	box->hovered = true;
	box->color.r += 0.2f, box->color.g += 0.2f, box->color.b += 0.2f;
}

/* Smoothly slows the velocity of the game object when released. CURRENTLY UNUSED */
/*local void Ease_obj(Game_state *game, Input *input)
{
	float old_spd = game->vel;

	//game->player_location.x += ((game->accel / 2) * Pow(game->time_speed, 2)) + (old_spd * game->time_speed); // (0.5at^2) + vt + pos

	if (game->right)	
		game->player_location.x += ((game->vel + old_spd) / 2) * game->time_speed;
	if (game->left)
		game->player_location.x -= ((game->vel + old_spd) / 2) * game->time_speed;

	game->vel -= (game->time_speed * game->accel);

	if (game->vel <= game->deadzone)
	{
		game->vel = 0;
		game->sliding = game->left = game->right = false;
	}
}*/

local void D_America(Game_state *game, float cd)
{
	if (++game->color_state > 2)
		game->color_state = 0;
	if (game->color_state == 0)
		game->color = red;
	else if (game->color_state == 1)
		game->color = white;
	else if (game->color_state == 2)
		game->color = blue;

	game->cooldown = cd;
}

/* The main engine loop. */
extern "C" GAME_UPDATE(Game_update)
{
	Game_state *game_state = (Game_state *)memory->perma_storage;
	World *world = &game_state->world;
	Rect screen_size = { { 0, 0 }, { (float)buffer->w, (float)buffer->h } };
	Point win_center = Get_window_center(*buffer);
	input->mouse_pos = { (float)input->mouse_x, (float)input->mouse_y };

	char *femon = "..\\data\\femon_icon.bmp";
	char *cursor = "..\\data\\mockup_cursor.bmp";
	char *floran = "..\\data\\Floran_Femon.bmp";
	char *hvm = "..\\data\\HVM.bmp";
	char *test = "..\\data\\alpha_test.bmp";

	if (!memory->initialized)
	{
		memory->initialized = true;
		game_state->time_accel = 1;
		game_state->color = white, game_state->color_state = 1;
		game_state->cursor_img = D_Load_bmp_image(threading, memory->D_platform_read, cursor);
		game_state->bmp = D_Load_bmp_image(threading, memory->D_platform_read, femon);

		world->game_unit = 1.0f;
		world->pixel_unit = 10;
		world->unit_to_pixels = (float)world->pixel_unit / world->game_unit;
		world->menu.area = { { (float)buffer->w - 330, (float)buffer->h - 520 }, { (float)buffer->w, (float)buffer->h - 60 } };
		world->menu.color = { 0.2f, 0.2f, 0.2f };

		game_state->destination = { 100, -200 };
		game_state->deadzone = 0.1f;
	};
	
	game_state->time_speed = input->time_speed * game_state->time_accel;
	game_state->cursor = { (float)input->mouse_x, (float)input->mouse_y };
	
	game_state->player.sprite = Create_square
	(
		{ game_state->player.location.x, -game_state->player.location.y },
		(float)world->game_unit * world->unit_to_pixels
	);
	game_state->npc.sprite = Create_square
	(
		{ game_state->npc.location.x, -game_state->npc.location.y},
		2 * world->unit_to_pixels
	);

	game_state->player.origin = { (game_state->player.sprite.p2.x + game_state->player.sprite.p1.x) / 2, -(game_state->player.sprite.p2.y + game_state->player.sprite.p1.y) / 2 };
	game_state->npc.origin = { (game_state->npc.sprite.p2.x + game_state->npc.sprite.p1.x) / 2, -(game_state->npc.sprite.p2.y + game_state->npc.sprite.p1.y) / 2 };

	Draw_fill_rect(buffer, screen_size, black); /* Clear screen */

	Draw_bitmap(buffer, { 75 , 0 }, game_state->bmp);

	if (game_state->cooldown > 0)
		game_state->cooldown -= 10.0f * game_state->time_speed;

	float player_spd = 1000.0f;

	float diagonal_accel = 0.7071067811865475f; /* Square root of 0.5. */

	Box menu_toggle = {};
	menu_toggle.area = { { world->menu.area.p1.x - 50, (world->menu.area.h / 2) - 50 }, { world->menu.area.p1.x - 30, (world->menu.area.h / 2) + 50 } };
	menu_toggle.color = { 0.4f, 0.4f, 0.4f };

	for (int controller_index = 0; controller_index < ARRAY_COUNT(input->controller); ++controller_index)
	{
		Controller *controller = Get_controller(input, controller_index);
		if (controller->connected)
		{
			if (controller->is_analog) {}

			Point next_move = {};

			/* Keyboard bindings */
			if (controller->a_up.ended_down)
			{
				game_state->player.location = { 100, -100 };
			}
			if (controller->a_down.ended_down)
			{
				/* Move 500 units */
				if (!game_state->npc_move)
				{
					game_state->npc.location = { 400, -100 };
					game_state->marked_location = game_state->npc.location;
					game_state->distance = -(game_state->npc.location - game_state->destination);
					game_state->npc.vel = game_state->distance * 0.8f;
					game_state->npc_move = true;
				}
			}

			if (controller->action.ended_down)
			{
				if (world->menu_state == 0) 
				{
					world->menu_force = 2500;
					if (!world->menu_closed)
						world->menu_state = 1;
					else if (world->menu_closed)
						world->menu_state = 2;
				}
				
				if (game_state->cooldown <= 0)
					D_America(game_state, 4);
			}
			if (controller->up.ended_down)
			{
				if (!game_state->sliding)
					next_move.y += player_spd;
			}

			if (controller->down.ended_down)
			{
				if (!game_state->sliding)
					next_move.y -= player_spd;
			}

			if (controller->right.ended_down)
			{
				if (!game_state->sliding)
					next_move.x += player_spd;
			}

			if (controller->left.ended_down)
			{
				if (!game_state->sliding)
					next_move.x -= player_spd;
			}

			if (next_move.x && next_move.y)
				next_move *= diagonal_accel;

			if (controller->ff.ended_down)
			{
				if (game_state->time_accel == 1)
					game_state->time_accel = 2.5;
				else game_state->time_accel = 1;
			}

			if (!controller->a_up.ended_down)
				next_move += game_state->player.vel * -10.0f;

			if (next_move.x || next_move.y)
				game_state->player.location += ((next_move * 0.5) * Pow(game_state->time_speed, 2)) + (game_state->player.vel * game_state->time_speed);

			game_state->player.vel += next_move * game_state->time_speed;
		}
	}

	/* 
	*** DOT PRODUCT / INNER PRODUCT / SCALAR PRODUCT ***

	* Matrix Math - Vectors
	| a |					| c |
	|   | -> DOT PRODUCT ->	|   | = ac + bd
	| b |					| d |

	a * b = | a | * | b | * cos0 (cosine theta)

	*/

	/* Movement based on displacement */
		/* VARIABLES
			Character char,
			Point waypoint,
			float offset,
			float rate;
		*/
	/*
	if (game_state->npc_move)
	{
		if (game_state->npc.location.x >= game_state->marked_location.x + 100)
			game_state->npc_move = false;
		else
			game_state->npc.location += { 100 * game_state->time_speed, 0 };
	}
	*/

	/* Movement based on destination */
	if (game_state->npc_move)
	{
		/* If both values are not greater than the destination's coords, increment by a vertain value. Currently flawed and can break. */
		if (Round_float(Abs(game_state->npc.location.x)) >= Round_float(Abs(game_state->destination.x)) && Round_float(Abs(game_state->npc.location.y)) >= Round_float(Abs(game_state->destination.y)))
			game_state->npc_move = false;
		else
		{
			/* Smooth stop attempt. Will revisit
			if (-(game_state->npc.location.x - game_state->destination.x) > game_state->distance.x * 0.5f && -(game_state->npc.location.y - game_state->destination.y) > game_state->distance.y * 0.25f)
			{
				game_state->npc.vel = game_state->npc.vel - (game_state->npc.location - game_state->destination);
			}
			*/
			game_state->npc.location += game_state->npc.vel * game_state->time_speed;
		}
	}

	if (Is_inside(input->mouse_pos, menu_toggle.area))
	{
		if (input->mouse_buttons[0].ended_down && world->menu_state == 0)
		{
			world->menu_force = 2500;
			if (!world->menu_closed)
				world->menu_state = 1;
			else if (world->menu_closed)
				world->menu_state = 2;

			menu_toggle.color = { 0.2f, 0.2f, 0.2f };
		}
		Highlight_box(&menu_toggle);
	}

	if (world->menu_state != 0)
	{
		float accel = 0.0001f;
		float old_spd = world->menu_force;
		world->menu_force -= game_state->time_speed / accel;

		if (world->menu_force <= 5)
		{
			if (!world->menu_closed)
				world->menu_closed = true;
			else
				world->menu_closed = false;
			world->menu_state = 0;
		}
		
		if (world->menu_state == 1)
		{
			world->menu.area.p1.x += ((world->menu_force + old_spd) / 2) * game_state->time_speed;
		}
		else if (world->menu_state == 2)
		{
			world->menu.area.p1.x -= ((world->menu_force + old_spd) / 2) * game_state->time_speed;
		}
		else
			world->menu_state = 0;
	}

	Draw_fill_rect(buffer, game_state->npc.sprite, green);
	/* Render player */
	Draw_line_rect(buffer, game_state->player.sprite, game_state->color, 2);
	

	/* Render menu */
	Draw_outlined_rect(buffer, world->menu.area, world->menu.color, red, 10);
	Draw_outlined_rect(buffer, menu_toggle.area, menu_toggle.color, yellow, 10);
	Rect item_area = { { 0, 0 }, { 250, 100 } };
	Box item_box = { item_area, { 0.4f, 0.4f, 0.4f } };
	Box boxes[4];
	for (int i = 0; i < ARRAY_COUNT(boxes); i++)
	{
		float padding = 30;
		float spacing = i * ((world->menu.area.h - padding - item_area.h) / (ARRAY_COUNT(boxes) - 1));
		boxes[i].area =
		{
			{ item_area.p1.x + world->menu.area.p1.x + padding, world->menu.area.p1.y + padding + spacing },
			{ item_area.p2.x + world->menu.area.p2.x, item_area.h + padding + spacing }
		};
		boxes[i].color = item_box.color;
		if (Is_inside(input->mouse_pos, boxes[i].area))
		{
			if (input->mouse_buttons[0].ended_down)
			{
				Draw_fill_rect(buffer, Create_square
				(
					{ (float)world->pixel_unit, (float)world->pixel_unit * (i + 1) * 2 },
					(float)world->pixel_unit), yellow
				);
				boxes[i].color = { 0.2f, 0.2f, 0.2f };
			}
			else { Highlight_box(&boxes[i]); }
		}
		Draw_outlined_rect(buffer, boxes[i].area, boxes[i].color, red, 5);
	}

	/* Render cursor */
	Draw_bitmap(buffer, game_state->cursor, game_state->cursor_img); // Render cursor
}

local void Game_sound_output(Game_state *game, Sound_buffer *sbuffer, int volume)
{
	short *sample_out = sbuffer->samples;
	short sample_val = 0;
	for (int sample_index = 0; sample_index < sbuffer->sample_count; ++sample_index)
	{
#if 0
		sample_val = (short)(sine_wave * volume);
		float sine_wave = sinf(game->t_sine);
		int wave_period = sbuffer->samples_per_sec / game->hertz;

		game->t_sine += (2.0f * PI) / (float)wave_period;
		if (game->t_sine > 2.0f * PI)
			game->t_sine -= 2.0f * PI;
#else 
		*sample_out++ = sample_val;
		*sample_out++ = sample_val;
#endif
	}
}

extern "C" GAME_GET_SOUND_SAMPLES(Game_get_sound_samples)
{
	Game_state *game_state = (Game_state *)memory->perma_storage;
	Game_sound_output(game_state, sbuffer, 1000);
}

#if DEBUG_BUILD
/* No longer of any use; deprecated */
local void Render_tile_gradient(Pixel_buffer *buffer, uchar x_offset, uchar y_offset)
{
	uchar *row = (uchar*)buffer->memory;
	for (int y = 0; y < buffer->h; y++)
	{
		uint *pixel = (uint*)row;
		/* THIS IS LITTLE ENDIAN (LAST TO FIRST) */
		for (int x = 0; x < buffer->w; x++)
		{
			uchar B = (uchar)-(x + x_offset);
			uchar G = (uchar)(x + x_offset);
			uchar R = (uchar)-(y + y_offset);
			*pixel++ = (uint)((R << 8) | (G << 16) | B);
		}
		row += buffer->pitch;
	}
}
#endif