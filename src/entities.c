#include "entities.h"

#include <Empire/assets.h>
#include <Empire/generated/assets_generated.h>
#include <Empire/level.h>
#include <Empire/math.inl>
#include <Empire/miniaudio.h>
#include <Empire/text.h>
#include <SDL3/SDL.h>

#define ANIMATION_SPEED 0.15f
#define DECO_ANIMATION_SPEED 0.5f
#define MAX_WEAPON_CONFIGS 8
#define NULL_WEAPON_CONFIG 0

float SPRITE_MAGNIFICATION = 4.0f;
emp_G* G;
emp_enemy_conf_t* enemy_confs[16];
emp_weapon_conf_t* weapons[10];

emp_weapon_conf_t* particle_config;
emp_weapon_conf_t* text_particle_config;

void emp_ka_ching(emp_vec2_t pos);
void emp_damage_number(emp_vec2_t pos, u32 number);

static SDL_Cursor* g_hovered_cursor;
static SDL_Cursor* g_idle_cursor;

SDL_Cursor* CreateCyanBlueCursor(u32 X, u32 A, u32 B, u32 C, u32 D)
{
	enum {
		BASE_SIZE = 16, // Square canvas
		SCALE = 4 // 16 * 2 = 32x32 cursor (use SCALE = 4 for 64x64)
	};
	enum {
		W = BASE_SIZE * SCALE,
		H = BASE_SIZE * SCALE
	};

#define _ 0x00000000

	// Padded to a 16x16 square grid
	const Uint32 base[BASE_SIZE][BASE_SIZE] = {
		{ X, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ X, X, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ X, A, X, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ X, B, A, X, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ X, C, B, A, X, _, _, _, _, _, _, _, _, _, _, _ },
		{ X, C, C, B, A, X, _, _, _, _, _, _, _, _, _, _ },
		{ X, D, C, B, B, A, X, _, _, _, _, _, _, _, _, _ },
		{ X, D, C, C, B, B, A, X, _, _, _, _, _, _, _, _ },
		{ X, D, D, C, C, X, X, _, _, _, _, _, _, _, _, _ },
		{ X, D, D, X, X, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, X, X, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
		{ _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ },
	};

#undef _

	// In SDL3 use SDL_CreateSurface; in SDL2 use SDL_CreateRGBSurfaceWithFormat
	SDL_Surface* surface = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_ARGB8888);
	if (!surface) {
		return NULL;
	}

	Uint32* pixels = (Uint32*)surface->pixels;
	int pitch_pixels = surface->pitch / sizeof(Uint32);

	for (int y = 0; y < BASE_SIZE; y++) {
		for (int x = 0; x < BASE_SIZE; x++) {
			Uint32 color = base[y][x];
			for (int dy = 0; dy < SCALE; dy++) {
				for (int dx = 0; dx < SCALE; dx++) {
					pixels[(y * SCALE + dy) * pitch_pixels + (x * SCALE + dx)] = color;
				}
			}
		}
	}

	SDL_Cursor* cursor = SDL_CreateColorCursor(surface, 0, 0);

	SDL_DestroySurface(surface);

	return cursor;
}

static void emp_idle_cursor()
{
	if (g_idle_cursor) {
		SDL_SetCursor(g_idle_cursor);
	}
}

static void emp_hover_cursor()
{
	if (g_hovered_cursor) {
		SDL_SetCursor(g_hovered_cursor);
	}
}

typedef struct emp_music_player
{
	ma_sound sounds[2];
	ma_decoder decoders[2];
	i32 track_steps[2];
	i32 current_track;
	bool initialized;
} emp_music_player;

void emp_music_player_init(void)
{
	G->music_player = (emp_music_player*)SDL_malloc(sizeof(*G->music_player));
	emp_music_player* music = G->music_player;
	SDL_memset(music, 0, sizeof(*music));

	i32 track_steps[] = {
		2300,
		9000,
	};

	music->track_steps[0] = track_steps[0];
	music->track_steps[1] = track_steps[1];
	music->current_track = 0;
	music->initialized = false;

	if (!G->mixer)
		return;

	typedef struct
	{
		ma_decoder decoder;
		const void* data;
		size_t size;
	} emp_audio_t;

	emp_asset_t* tracks[] = {
		&G->assets->ogg->calm_music_loopable,
		&G->assets->ogg->intense_music_loopable,
	};

	for (int i = 0; i < 2; i++) {
		emp_audio_t* audio = tracks[i]->handle;

		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, G->mixer->sampleRate);
		ma_result result = ma_decoder_init_memory(audio->data, audio->size, &config, &music->decoders[i]);
		SDL_assert(result == MA_SUCCESS && "Failed to init music decoder");
		result = ma_sound_init_from_data_source(G->mixer, &music->decoders[i], 0, NULL, &music->sounds[i]);
		SDL_assert(result == MA_SUCCESS && "Failed to init music sound");
		ma_sound_set_looping(&music->sounds[i], MA_TRUE);
	}

	music->initialized = true;
	ma_sound_set_fade_in_milliseconds(&music->sounds[0], 0, 1, 1000);
	ma_sound_start(&music->sounds[0]);
}

typedef struct emp_roamer_data_t
{
	float time_until_change;
} emp_roamer_data_t;

typedef struct emp_chaser_data_t
{
	float radius;
} emp_chaser_data_t;

typedef struct emp_chest_data_t
{
	u32 item_spawn;
} emp_chest_data_t;

u32 rng_state;

typedef struct emp_player_conf_t
{
	float speed;
} emp_player_conf_t;

emp_player_conf_t get_player_conf()
{
	return (emp_player_conf_t) { .speed = 60.0f };
}

#define SOUND_POOL_SIZE 32

typedef struct
{
	ma_sound sound;
	ma_decoder decoder;
	bool in_use;
} emp_sound_slot_t;

static emp_sound_slot_t g_sound_pool[SOUND_POOL_SIZE];

void play_one_shot(emp_asset_t* asset)
{
	if (!asset || !asset->handle || !G->mixer)
		return;

	typedef struct
	{
		ma_decoder decoder;
		const void* data;
		size_t size;
	} emp_audio_t;
	emp_audio_t* audio = asset->handle;

	// Find a free slot (not playing)
	emp_sound_slot_t* slot = NULL;
	for (int i = 0; i < SOUND_POOL_SIZE; i++) {
		if (!g_sound_pool[i].in_use) {
			slot = &g_sound_pool[i];
			break;
		}
		// Check if a slot finished playing
		if (!ma_sound_is_playing(&g_sound_pool[i].sound)) {
			ma_sound_uninit(&g_sound_pool[i].sound);
			ma_decoder_uninit(&g_sound_pool[i].decoder);
			g_sound_pool[i].in_use = false;
			slot = &g_sound_pool[i];
			break;
		}
	}

	if (!slot)
		return; // All slots busy

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, G->mixer->sampleRate);
	ma_result result = ma_decoder_init_memory(audio->data, audio->size, &config, &slot->decoder);
	if (result != MA_SUCCESS)
		return;

	result = ma_sound_init_from_data_source(G->mixer, &slot->decoder, 0, NULL, &slot->sound);
	if (result != MA_SUCCESS) {
		ma_decoder_uninit(&slot->decoder);
		return;
	}

	slot->in_use = true;
	ma_sound_start(&slot->sound);
}

void play_one_shot_bullet(emp_weapon_conf_t* weapon)
{
	double current_time = G->args->global_time;
	if (current_time - weapon->last_played_ms < weapon->delay_between_shots) {
		return;
	}

	play_one_shot(weapon->sound_asset);

	weapon->last_played_ms = G->args->global_time;
}

#define SECTION_WIDTH 480.0f
#define SECTION_HEIGHT 288.0f

emp_vec2_t get_camera_pos(void)
{
	float cam_x = (floorf(G->player->pos.x / SECTION_WIDTH) * SECTION_WIDTH) + (SECTION_WIDTH * 0.5f);
	float cam_y = (floorf(G->player->pos.y / SECTION_HEIGHT) * SECTION_HEIGHT) + (SECTION_HEIGHT * 0.5f);
	return (emp_vec2_t) { .x = cam_x, .y = cam_y };
}

emp_vec2_t render_offset(void)
{
	int render_w, render_h;
	SDL_Window* window = SDL_GetRenderWindow(G->renderer);
	SDL_GetWindowSize(window, &render_w, &render_h);
	return (emp_vec2_t) { .x = (float)render_w / 2.0f, .y = (float)render_h / 2.0f };
}

void draw_rect_at(emp_vec2_t pos, float size, u8 r, u8 g, u8 b, u8 a)
{
	SDL_FRect rect;
	float screen_size = size * SPRITE_MAGNIFICATION;
	float sx = pos.x * SPRITE_MAGNIFICATION;
	float sy = pos.y * SPRITE_MAGNIFICATION;
	rect.x = sx - (screen_size / 2);
	rect.y = sy - (screen_size / 2);
	rect.w = screen_size;
	rect.h = screen_size;

	emp_vec2_t cam = get_camera_pos();
	rect.x -= cam.x * SPRITE_MAGNIFICATION;
	rect.y -= cam.y * SPRITE_MAGNIFICATION;

	emp_vec2_t offset = render_offset();
	rect.x += offset.x;
	rect.y += offset.y;

	SDL_SetRenderDrawColor(G->renderer, r, g, b, a);
	if (G->player->wants_debug) {
		SDL_RenderRect(G->renderer, &rect);
	}
}

SDL_FRect render_rect(emp_vec2_t pos, emp_texture_t* texture)
{
	SDL_FRect rect;
	float width = texture->width * SPRITE_MAGNIFICATION;
	float height = texture->height * SPRITE_MAGNIFICATION;
	float sx = pos.x * SPRITE_MAGNIFICATION;
	float sy = pos.y * SPRITE_MAGNIFICATION;
	rect.x = sx - (width / 2);
	rect.y = sy - (height / 2);
	rect.w = width;
	rect.h = height;

	emp_vec2_t cam = get_camera_pos();
	rect.x -= cam.x * SPRITE_MAGNIFICATION;
	rect.y -= cam.y * SPRITE_MAGNIFICATION;

	emp_vec2_t offset = render_offset();
	rect.x += offset.x;
	rect.y += offset.y;

	if (G->player->wants_debug) {
		SDL_RenderRect(G->renderer, &rect);
	}

	return rect;
}

SDL_FRect player_rect(emp_vec2_t pos, emp_texture_t* texture)
{
	return render_rect(pos, texture);
}

static SDL_FRect render_rect_tile(emp_vec2_t pos, float grid_size)
{
	float size = grid_size * SPRITE_MAGNIFICATION;
	float sx = pos.x * SPRITE_MAGNIFICATION;
	float sy = pos.y * SPRITE_MAGNIFICATION;
	float x = sx - (size / 2);
	float y = sy - (size / 2);
	float w = size;
	float h = size;

	emp_vec2_t cam = get_camera_pos();
	x -= cam.x * SPRITE_MAGNIFICATION;
	y -= cam.y * SPRITE_MAGNIFICATION;

	emp_vec2_t offset = render_offset();
	x += offset.x;
	y += offset.y;

	x = floorf(x);
	y = floorf(y);
	w = ceilf(size);
	h = ceilf(size);

	SDL_FRect rect = { x, y, w, h };
	if (G->player->wants_debug) {
		SDL_RenderRect(G->renderer, &rect);
	}

	return rect;
}

emp_vec2i_t get_tile(emp_vec2_t pos)
{
	int tile_x = (int)(floorf(pos.x / EMP_TILE_SIZE));
	int tile_y = (int)(floorf(pos.y / EMP_TILE_SIZE));

	return (emp_vec2i_t) { .x = tile_x, .y = tile_y };
}

bool tile_in_bounds(emp_vec2i_t tile)
{
	if (tile.x >= 0 && tile.x < (int)EMP_LEVEL_WIDTH && tile.y >= 0 && tile.y < (int)EMP_LEVEL_HEIGHT) {
		return true;
	}
	return false;
}

bool check_overlap_map(emp_vec2_t pos)
{
	emp_vec2i_t tile = get_tile(pos);

	if (tile_in_bounds(tile)) {
		u64 index = tile.y * EMP_LEVEL_WIDTH + tile.x;
		emp_tile_t* tile_data = G->level->tiles + index;
		return tile_data->state != emp_tile_state_none;
	}

	return true;
}

SDL_FRect source_rect(emp_texture_t* texture);

bool check_overlap_bullet_enemy(emp_bullet_t* bullet, emp_enemy_t* enemy)
{
	emp_texture_t* texture = enemy->texture_asset->handle;
	float size = texture->width / 3.0f;

	SDL_FRect bullet_size = source_rect(bullet->texture_asset->handle);
	SDL_FRect enemy_size = source_rect(enemy->texture_asset->handle);

	emp_vec2_t bullet_center = {
		.x = bullet->pos.x + (bullet_size.w * 0.5f),
		.y = bullet->pos.y + (bullet_size.h * 0.5f),
	};

	emp_vec2_t enemy_center = {
		.x = enemy->pos.x + (enemy_size.w * 0.5f),
		.y = enemy->pos.y + (enemy_size.h * 0.5f),
	};

	return emp_vec2_dist_sq(bullet_center, enemy_center) < size * size;
}

bool check_overlap_bullet_player(emp_bullet_t* bullet, emp_vec2_t pos)
{
	float size = 32.0f / 2.0f;
	return emp_vec2_dist_sq(bullet->pos, pos) < size * size;
}

bool check_overlap_bullet(emp_bullet_t* bullet, emp_vec2_t pos, float size)
{
	return emp_vec2_dist_sq(bullet->pos, pos) < size * size;
}

u64 index_from_tile(emp_vec2i_t tile)
{
	return tile.y * EMP_LEVEL_WIDTH + tile.x;
}

void add_enemy_to_tile(emp_enemy_t* enemy)
{
	emp_vec2i_t tile = get_tile(enemy->pos);

	if (tile_in_bounds(tile)) {
		u64 index = index_from_tile(tile);
		emp_enemy_t** first_enemy = &G->level->enemy_in_tile[index];
		enemy->next_in_tile = *first_enemy;
		*first_enemy = enemy;
		G->level->enemy_in_tile[index] = enemy;
	} else {
		enemy->next_in_tile = NULL;
	}
}

SDL_FRect source_rect(emp_texture_t* texture)
{
	u32 total_frames = texture->columns * texture->rows;
	u32 slot = (u32)(G->args->global_time / ANIMATION_SPEED) % total_frames;

	u32 column = slot % texture->columns;
	u32 row = slot / texture->columns;

	SDL_FRect rect;

	rect.x = (float)(column * texture->source_size);
	rect.y = (float)(row * texture->source_size);
	rect.w = (float)texture->source_size;
	rect.h = (float)texture->source_size;

	return rect;
}

u32 simple_rng(u32* state)
{
	*state = *state * 1103515245u + 12345u;
	return (*state >> 16) & 0x7FFF;
}

float random_float(float min, float max)
{
	float normalized = (float)simple_rng(&rng_state) / 32767.0f;
	return min + normalized * (max - min);
}

void enemy_chaser_update(emp_enemy_t* enemy)
{
	float dt = G->args->dt;

	enemy->direction = emp_vec2_sub(G->player->pos, enemy->pos);
	enemy->direction = emp_vec2_normalize(enemy->direction);

	enemy->flip = enemy->direction.x > 0.0f;
	emp_vec2_t movement = emp_vec2_mul(enemy->direction, enemy->speed * dt);

	float osc_amplitude = 12.5f;
	float osc_frequency = 1.5f;
	float phase_offset = (float)((uintptr_t)enemy % 1000) * 0.01f;
	movement.y += SDL_sinf((float)G->args->global_time * osc_frequency + phase_offset) * osc_amplitude * dt;

	emp_vec2_t new_pos = emp_vec2_add(enemy->pos, movement);
	enemy->pos = new_pos;
}

void enemy_chest_update(emp_enemy_t* enemy)
{
	if (enemy->health <= 0.0f) {
		G->player->weapon_index = SDL_min(G->player->weapon_index + 1, MAX_WEAPON_CONFIGS);

		emp_ka_ching(enemy->pos);
	}
}

void enemy_roamer_update(emp_enemy_t* enemy)
{
	emp_roamer_data_t* data = (emp_roamer_data_t*)&enemy->dynamic_data[0];
	float dt = G->args->dt;

	data->time_until_change -= dt;

	if (data->time_until_change <= 0.0f) {
		float angle = (float)(simple_rng(&rng_state) % 360);
		enemy->direction = emp_vec2_rotate((emp_vec2_t) { 1.0f, 0.0f }, angle);

		data->time_until_change = 0.3f + (float)(simple_rng(&rng_state) % 100) * 0.005f;
	}

	emp_vec2_t movement = emp_vec2_mul(enemy->direction, enemy->speed * dt);

	emp_vec2_t new_pos = emp_vec2_add(enemy->pos, movement);
	if (!check_overlap_map(new_pos)) {
		enemy->pos = new_pos;
	} else {
		float angle = (float)(simple_rng(&rng_state) % 360);
		enemy->direction = emp_vec2_rotate((emp_vec2_t) { 1.0f, 0.0f }, angle);
		data->time_until_change = 0.3f + (float)(simple_rng(&rng_state) % 100) * 0.005f;
	}
}

void bullet_text_render(emp_bullet_t* bullet)
{
	SDL_FRect target = render_rect(bullet->pos, G->assets->png->bullet2_8.handle);
	char buf[64];
	SDL_snprintf(buf, 64, "%.0f", bullet->damage);
	emp_draw_text(target.x, target.y, buf, 255, 255, 180, &G->assets->ttf->asepritefont);
}

#define ENEMY_CONF_CHEST 4

void emp_init_enemy_configs()
{
	enemy_confs[0] = SDL_malloc(sizeof(emp_enemy_conf_t));
	emp_enemy_conf_t* e0 = enemy_confs[0];
	e0->health = 10;
	e0->speed = 30.0f;
	e0->texture_asset = &G->assets->png->enemy1_32;
	e0->data_size = sizeof(emp_roamer_data_t);
	e0->update = enemy_roamer_update;
	e0->late_update = NULL;

	enemy_confs[1] = SDL_malloc(sizeof(emp_enemy_conf_t));
	emp_enemy_conf_t* e1 = enemy_confs[1];
	e1->health = 50;
	e1->speed = 30.0f;
	e1->texture_asset = &G->assets->png->boss2_64;
	e1->data_size = sizeof(emp_roamer_data_t);
	e1->update = enemy_roamer_update;
	e1->late_update = NULL;

	enemy_confs[2] = SDL_malloc(sizeof(emp_enemy_conf_t));
	emp_enemy_conf_t* e2 = enemy_confs[2];
	e2->health = 12;
	e2->speed = 30.0f;
	e2->texture_asset = &G->assets->png->enemy3_32;
	e2->data_size = sizeof(emp_chaser_data_t);
	e2->update = enemy_chaser_update;
	e2->late_update = NULL;

	enemy_confs[3] = SDL_malloc(sizeof(emp_enemy_conf_t));
	emp_enemy_conf_t* e3 = enemy_confs[3];
	e3->health = 100;
	e3->speed = 30.0f;
	e3->texture_asset = &G->assets->png->boss1_64;
	e3->data_size = sizeof(emp_roamer_data_t);
	e3->update = enemy_chaser_update;
	e3->late_update = NULL;

	// index 4
	enemy_confs[ENEMY_CONF_CHEST] = SDL_malloc(sizeof(emp_enemy_conf_t));
	emp_enemy_conf_t* e4 = enemy_confs[4];
	e4->health = 1;
	e4->speed = 0.0f;
	e4->texture_asset = &G->assets->png->chest1_32;
	e4->data_size = sizeof(emp_chest_data_t);
	e4->update = NULL;
	e4->late_update = enemy_chest_update;
}

void emp_init_weapon_configs()
{
	SDL_memset(weapons, 0, sizeof(weapons));
	// 0 is NUll config
	weapons[0] = SDL_malloc(sizeof(emp_weapon_conf_t));
	weapons[0]->delay_between_shots = 1000.0f;
	weapons[0]->num_shots = 0;

	weapons[1] = SDL_malloc(sizeof(emp_weapon_conf_t));
	weapons[1]->delay_between_shots = 0.5f;
	weapons[1]->num_shots = 1;
	weapons[1]->shots[0] = (emp_bullet_conf_t) {
		.speed = 125.0f,
		.start_angle = 0.0f,
		.lifetime = 4.0f,
		.texture_asset = &G->assets->png->bullet_8,
		.damage = 1.0,
	};
	weapons[1]->sound_asset = &G->assets->ogg->shot1;
	weapons[1]->last_played_ms = 0;

	weapons[2] = SDL_malloc(sizeof(emp_weapon_conf_t));
	weapons[2]->delay_between_shots = 0.4f;
	weapons[2]->num_shots = 1;
	weapons[2]->shots[0] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 0.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8,
	};
	weapons[2]->sound_asset = &G->assets->ogg->shot1;
	weapons[2]->last_played_ms = 0;

	weapons[3] = SDL_malloc(sizeof(emp_weapon_conf_t)); // 3 shot
	weapons[3]->delay_between_shots = 0.3f;
	weapons[3]->num_shots = 3;
	weapons[3]->last_played_ms = 0;
	weapons[3]->shots[0] = (emp_bullet_conf_t) {
		.speed = 224.0f,
		.start_angle = 0.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[3]->shots[1] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -15.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[3]->shots[2] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 15.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[3]->sound_asset = &G->assets->ogg->shot2;

	weapons[4] = SDL_malloc(sizeof(emp_weapon_conf_t)); // 5 shot
	weapons[4]->delay_between_shots = 0.3f;
	weapons[4]->num_shots = 5;
	weapons[4]->last_played_ms = 0;
	weapons[4]->shots[0] = (emp_bullet_conf_t) {
		.speed = 224.0f,
		.start_angle = 0.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[4]->shots[1] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -15.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet2_8
	};
	weapons[4]->shots[2] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 15.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet2_8
	};
	weapons[4]->shots[3] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -30.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[4]->shots[4] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 30.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[4]->sound_asset = &G->assets->ogg->shot3;

	weapons[5] = SDL_malloc(sizeof(emp_weapon_conf_t)); // full circle
	weapons[5]->delay_between_shots = 0.3f;
	weapons[5]->num_shots = 12;
	weapons[5]->sound_asset = &G->assets->ogg->shot1;
	weapons[5]->last_played_ms = 0;
	weapons[5]->shots[0] = (emp_bullet_conf_t) {
		.speed = 224.0f,
		.start_angle = 0.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[1] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -30.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[2] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 30.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[3] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -60.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[4] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 60.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[5] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -90.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[6] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 90.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[7] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -120.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[8] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 120.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[9] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -150.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[10] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 150.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[11] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = 180.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};
	weapons[5]->shots[12] = (emp_bullet_conf_t) {
		.speed = 200.0f,
		.start_angle = -180.0f,
		.lifetime = 3.0f,
		.damage = 1.0,
		.texture_asset = &G->assets->png->bullet_8
	};

	int total_bullets = 24;
	weapons[6] = SDL_malloc(sizeof(emp_weapon_conf_t)); // simple double circle
	weapons[6]->delay_between_shots = 0.3f;
	weapons[6]->num_shots = total_bullets;
	weapons[6]->sound_asset = &G->assets->ogg->shot2;
	weapons[6]->last_played_ms = 0;
	for (int i = 0; i < total_bullets; i++) {
		float angle = i * 15.0f;
		float current_speed = (i % 2) ? 75.0f : 100.0f;

		weapons[6]->shots[i] = (emp_bullet_conf_t) {
			.speed = current_speed,
			.start_angle = angle,
			.lifetime = 3.0f,
			.damage = 1.0,
			.texture_asset = &G->assets->png->bullet_8
		};
	}

	weapons[7] = SDL_malloc(sizeof(emp_weapon_conf_t)); // pretty double circle
	weapons[7]->delay_between_shots = 0.5f;
	weapons[7]->num_shots = total_bullets;
	weapons[7]->last_played_ms = 0;
	weapons[7]->sound_asset = &G->assets->ogg->shot3;
	for (int i = 0; i < total_bullets; i++) {
		float angle = i * 15.0f;
		float current_speed = (i % 2) ? 75.0f : 100.0f;

		weapons[7]->shots[i] = (emp_bullet_conf_t) {
			.speed = current_speed,
			.start_angle = angle,
			.lifetime = 3.0f,
			.damage = 1.0,
			.texture_asset = &G->assets->png->bullet2_8
		};
	}

	weapons[8] = SDL_malloc(sizeof(emp_weapon_conf_t)); // pretty half circle
	weapons[8]->delay_between_shots = 0.5f;
	weapons[8]->num_shots = total_bullets / 2 + 1;
	weapons[8]->last_played_ms = 0;
	weapons[8]->sound_asset = &G->assets->ogg->shot3;
	for (int i = 0; i < total_bullets; i++) {
		float angle = 90 - i * 15.0f;
		float current_speed = (i % 2) ? 75.0f : 100.0f;

		weapons[8]->shots[i] = (emp_bullet_conf_t) {
			.speed = current_speed,
			.start_angle = angle,
			.lifetime = 3.0f,
			.damage = 1.0,
			.texture_asset = &G->assets->png->bullet3_8
		};
	}

	particle_config = SDL_malloc(sizeof(emp_weapon_conf_t));
	particle_config->delay_between_shots = 0.5f;
	particle_config->num_shots = total_bullets;
	particle_config->last_played_ms = 0;
	particle_config->sound_asset = &G->assets->ogg->shot3;
	for (int i = 0; i < total_bullets; i++) {
		float angle = i * 15.0f;
		float current_speed = (i % 2) ? 75.0f : 100.0f;

		particle_config->shots[i] = (emp_bullet_conf_t) {
			.speed = current_speed,
			.start_angle = angle,
			.lifetime = 3.0f,
			.damage = 1.0,
			.texture_asset = &G->assets->png->bullet4_8
		};
	}

	text_particle_config = SDL_malloc(sizeof(emp_weapon_conf_t));
	text_particle_config->num_shots = 1;
	text_particle_config->sound_asset = NULL;
	text_particle_config->shots[0] = (emp_bullet_conf_t) {
		.speed = 60.0f,
		.start_angle = 0.0,
		.lifetime = 1.0f,
		.damage = 0.0,
		.texture_asset = &G->assets->png->bullet4_8,
		.custom_render = bullet_text_render,
	};
}

emp_bullet_h emp_create_bullet()
{
	for (u32 i = 1; i < EMP_MAX_BULLETS; ++i) {
		emp_bullet_t* bullet = &G->bullets[i];
		if (!bullet->alive) {
			bullet->generation++;
			bullet->pos = (emp_vec2_t) { 0 };
			bullet->vel = (emp_vec2_t) { 0 };
			bullet->life_left = 0.0f;
			bullet->damage = 0.0f;
			bullet->texture_asset = NULL;
			bullet->alive = true;
			return (emp_bullet_h) { .index = i, .generation = bullet->generation };
		}
	}

	assert(false && "out of bullets");

	return (emp_bullet_h) { 0 };
}

emp_bullet_conf_t get_bullet1()
{
	emp_bullet_conf_t bullet = (emp_bullet_conf_t) { .speed = 250.0f, .texture_asset = &G->assets->png->bullet_8 };
	return bullet;
}

void spawn_bullets(emp_vec2_t pos, emp_vec2_t direction, bullet_mask mask, emp_weapon_conf_t* conf)
{
	for (u32 i = 0; i < conf->num_shots; ++i) {
		emp_bullet_conf_t bullet_conf = conf->shots[i];
		emp_bullet_h bullet_handle = emp_create_bullet();
		emp_bullet_t* bullet = &G->bullets[bullet_handle.index];
		bullet->vel = emp_vec2_mul(emp_vec2_normalize(direction), bullet_conf.speed);
		if (bullet_conf.start_angle != 0.0f) {
			bullet->vel = emp_vec2_rotate(bullet->vel, bullet_conf.start_angle);
		}
		bullet->life_left = bullet_conf.lifetime;
		bullet->damage = bullet_conf.damage;
		bullet->pos = pos;
		bullet->texture_asset = bullet_conf.texture_asset;
		bullet->mask = mask;
		bullet->custom_render = bullet_conf.custom_render;
	}
	if (conf->sound_asset) {
		play_one_shot_bullet(conf);
	}
}

u32 emp_create_player()
{
	G->player[0].alive = true;
	G->player[0].generation = 1;
	G->player[0].weapon_index = 2;
	G->player[0].health = 20;
	G->player[0].max_health = 20;
	G->player[0].last_damage_time = 0;
	G->player[0].died_at_time = 0;
	G->player[0].vel = (emp_vec2_t) { 0 };
	G->player[0].prev_w_pressed = false;
	G->player[0].prev_s_pressed = false;
	G->player[0].prev_a_pressed = false;
	G->player[0].prev_d_pressed = false;
	G->player[0].timer = 0.0f;
	G->player[0].bubble_timer = 0.0f;
	return 0;
}

emp_enemy_h emp_create_enemy(emp_vec2_t pos, u32 enemy_conf_index, float health, float movement_speed, u32 weapon_index, emp_spawner_h spawned_by)
{
	for (u32 i = 1; i < EMP_MAX_ENEMIES; ++i) {
		emp_enemy_t* enemy = &G->enemies[i];
		if (!enemy->alive) {
			emp_enemy_conf_t* conf = enemy_confs[enemy_conf_index];
			SDL_memset(&enemy->dynamic_data, 0, 64);
			enemy->pos = pos;

			emp_vec2_t player_pos = G->player->pos;
			emp_vec2_t dir = emp_vec2_normalize(emp_vec2_sub(enemy->pos, player_pos));

			float speed = movement_speed == 0.0f ? conf->speed : movement_speed;

			enemy->direction = dir;
			enemy->generation++;
			enemy->health = health == 0.0f ? conf->health : health;
			enemy->update = conf->update;
			enemy->late_update = conf->late_update;
			enemy->speed = random_float(speed * 0.8f, speed * 1.2f);
			enemy->texture_asset = conf->texture_asset;
			enemy->weapon = weapons[weapon_index];
			enemy->alive = true;
			enemy->spawned_by = spawned_by;
			enemy->enemy_shot_delay = 4.0;
			return (emp_enemy_h) { .index = i, .generation = enemy->generation };
		}
	}

	assert(false && "out of enemies");

	return (emp_enemy_h) { 0 };
}

void emp_create_chest(emp_vec2_t pos, u32 weapon_index)
{
	emp_enemy_h handle = emp_create_enemy(pos, ENEMY_CONF_CHEST, 0.0f, 0.0f, NULL_WEAPON_CONFIG, (emp_spawner_h) { 0 });

	// emp_enemy_t* enemy = &G->enemies[handle.index];
	(void)handle;
}

void emp_create_spawner(emp_vec2_t pos, float health, float enemy_health, float movement_speed, u32 enemy_conf_index, u32 weapon_index, float frequency, u32 limit)
{
	for (u32 i = 1; i < EMP_MAX_SPAWNERS; ++i) {
		emp_spawner_t* spawner = &G->spawners[i];
		if (!spawner->alive) {
			SDL_memset(spawner, 0, sizeof(*spawner));
			spawner->enemy_conf_index = enemy_conf_index;
			spawner->alive = true;
			spawner->frequency = frequency;
			spawner->accumulator = 0;
			spawner->health = health;
			spawner->enemy_health = enemy_health;
			spawner->weapon_index = weapon_index;
			spawner->movement_speed = movement_speed;
			spawner->x = pos.x;
			spawner->y = pos.y;
			spawner->limit = limit;
			return;
		}
	}
	assert(false && "out of enemies");
}

emp_bullet_generator_h emp_create_bullet_generator()
{
	for (u32 i = 0; i < EMP_MAX_BULLET_GENERATORS; ++i) {
		emp_bullet_generator_t* gen = &G->generators[i];
		if (!gen->alive) {
			gen->generation++;
			gen->alive = true;
			return (emp_bullet_generator_h) { .index = i, .generation = gen->generation };
		}
	}

	assert(false && "out of generators");

	return (emp_bullet_generator_h) { 0 };
}

void emp_destroy_bullet_generator(emp_bullet_generator_h handle)
{
}

void emp_music_player_update(emp_music_player* music)
{
	if (!music->initialized)
		return;

	i32 preferred_track = 0;
	for (i32 index = 0; index < SDL_arraysize(music->track_steps); index++) {
		if (G->player->pos.x < music->track_steps[index]) {
			preferred_track = index;
			break;
		}
	}
	preferred_track = SDL_min(preferred_track, SDL_arraysize(music->track_steps));
	if (preferred_track != music->current_track) {
		// Fade out current track
		ma_sound_set_fade_in_milliseconds(&music->sounds[music->current_track], -1, 0, 1000);
		ma_sound_set_stop_time_in_milliseconds(&music->sounds[music->current_track], 1000);

		// Start and fade in new track
		music->current_track = preferred_track;
		ma_sound_seek_to_pcm_frame(&music->sounds[music->current_track], 0);
		ma_sound_set_fade_in_milliseconds(&music->sounds[music->current_track], 0, 1, 1000);
		ma_sound_start(&music->sounds[music->current_track]);
	}
}

void emp_player_update(emp_player_t* player)
{
	const bool* state = SDL_GetKeyboardState(NULL);
	emp_player_conf_t conf = get_player_conf();
	float dt = G->args->dt;

	typedef enum {
		PLAYER_STATE_IDLE_LEFT,
		PLAYER_STATE_IDLE_RIGHT,
		PLAYER_STATE_SWIM_LEFT,
		PLAYER_STATE_SWIM_RIGHT,
		PLAYER_STATE_WALK_LEFT,
		PLAYER_STATE_WALK_RIGHT,
	} emp_player_state_t;

	if (player->alive && player->health <= 0 && player->died_at_time == 0) {
		player->died_at_time = G->args->global_time;
		player->alive = false;
	}

	if (!player->alive) {
		player->vel = (emp_vec2_t) { 0 };
		player->prev_w_pressed = false;
		player->prev_s_pressed = false;
		player->prev_a_pressed = false;
		player->prev_d_pressed = false;
	}

	emp_player_state_t current_state = player->flip ? PLAYER_STATE_IDLE_LEFT : PLAYER_STATE_IDLE_RIGHT;

	if (player->alive) {
		emp_vec2_t movement = { 0 };

		float speed = player->movement_speed == 0.0f ? conf.speed : player->movement_speed;
		const float stroke_impulse = speed * 0.33f;

		float bubble_push_x = 0.0f;
		float bubble_push_y = 0.0f;

		bool a_pressed = state[SDL_SCANCODE_A];
		bool a_tapped = a_pressed && !player->prev_a_pressed;
		player->prev_a_pressed = a_pressed;

		bool d_pressed = state[SDL_SCANCODE_D];
		bool d_tapped = d_pressed && !player->prev_d_pressed;
		player->prev_d_pressed = d_pressed;

		if (a_tapped) {
			player->flip = true;
			player->vel.x = -stroke_impulse;
			bubble_push_x += 1.0f;
		}
		if (d_tapped) {
			player->flip = false;
			player->vel.x = stroke_impulse;
			bubble_push_x -= 1.0f;
		}

		float drag = 110.0f;
		if (player->vel.x > 0.0f) {
			if (state[SDL_SCANCODE_D]) {
				drag = drag * 0.5f;
			}
			player->vel.x -= drag * dt;
			if (player->vel.x < 0.0f)
				player->vel.x = 0.0f;
		} else if (player->vel.x < 0.0f) {
			if (state[SDL_SCANCODE_A]) {
				drag = drag * 0.5f;
			}
			player->vel.x += drag * dt;
			if (player->vel.x > 0.0f)
				player->vel.x = 0.0f;
		}
		movement.x = player->vel.x * dt;

		bool w_pressed = state[SDL_SCANCODE_W];
		bool w_tapped = w_pressed && !player->prev_w_pressed;
		player->prev_w_pressed = w_pressed;

		if (w_tapped) {
			player->vel.y = -85.0f;
			bubble_push_y += 1.0f;
		}

		bool s_pressed = state[SDL_SCANCODE_S];
		bool s_tapped = s_pressed && !player->prev_s_pressed;
		player->prev_s_pressed = s_pressed;

		if (s_tapped) {
			player->vel.y = 65.0f;
			bubble_push_y -= 1.0f;
		}

		if (bubble_push_x != 0.0f || bubble_push_y != 0.0f) {
			emp_vec2_t bdir = emp_vec2_normalize((emp_vec2_t) { bubble_push_x, bubble_push_y });
			const emp_vec2_t bubble_pos = {
				.x = player->pos.x + bdir.x * 6.0f,
				.y = player->pos.y + bdir.y * 6.0f
			};

			for (int i = 0; i < 4; ++i) {
				emp_bullet_h bh = emp_create_bullet();
				if (bh.index != 0) {
					emp_bullet_t* b = &G->bullets[bh.index];
					b->pos = bubble_pos;
					b->pos.x += random_float(-3.0f, 3.0f);
					b->pos.y += random_float(-3.0f, 3.0f);

					float spd = random_float(40.0f, 75.0f);
					b->vel.x = bdir.x * spd + random_float(-8.0f, 8.0f);
					b->vel.y = bdir.y * spd + random_float(-8.0f, 8.0f);
					b->life_left = random_float(0.3f, 0.55f);
					b->damage = 0.0f;
					b->texture_asset = &G->assets->png->bullet4_8;
					b->mask = 0;
					b->custom_render = NULL;
				}
			}
		}

		const float gravity = 150.0f;

		float max_sink_speed = 28.0f;
		if (state[SDL_SCANCODE_S]) {
			max_sink_speed = max_sink_speed * 2.0f;
		}
		if (player->vel.y > 0.0f && state[SDL_SCANCODE_W]) {
			max_sink_speed = max_sink_speed * 0.5f;
		}

		player->vel.y += gravity * dt;
		if (player->vel.y > max_sink_speed) {
			player->vel.y = max_sink_speed;
		}
		movement.y = player->vel.y * dt;

		if (state[SDL_SCANCODE_U]) {
			player->timer = 0.0;
		}
		player->timer = player->timer + G->args->dt;

		if (state[SDL_SCANCODE_M]) {
			player->health = 0.0f;
		}

		if (state[SDL_SCANCODE_P]) {
			player->wants_debug = 1;
		} else {
			player->wants_debug = 0;
		}

		emp_vec2_t pos_dx = emp_vec2_addx(player->pos, movement);
		if (check_overlap_map(pos_dx)) {
			movement.x = 0;
			player->vel.x = 0.0f;
		}

		bool on_floor = false;
		emp_vec2_t pos_dy = emp_vec2_addy(player->pos, movement);
		if (check_overlap_map(pos_dy)) {
			if (movement.y > 0.0f) {
				on_floor = true;
			}
			movement.y = 0;
			if (player->vel.x > 0.0f) {
				if (state[SDL_SCANCODE_D]) {
					drag = drag * 0.5f;
				}
				player->vel.x -= drag * dt;
				if (player->vel.x < 0.0f)
					player->vel.x = 0.0f;
			} else if (player->vel.x < 0.0f) {
				if (state[SDL_SCANCODE_A]) {
					drag = drag * 0.5f;
				}
				player->vel.x += drag * dt;
				if (player->vel.x > 0.0f)
					player->vel.x = 0.0f;
			}
			player->vel.y = 0.0f;
		}

		player->pos = emp_vec2_add(player->pos, movement);

		bool is_moving_x = SDL_fabsf(player->vel.x) > 4.0f;

		// Just in case lol
		if (!on_floor) {
			on_floor = check_overlap_map((emp_vec2_t) { player->pos.x, player->pos.y + 2.0f });
		}
		if (!on_floor) {
			current_state = player->flip ? PLAYER_STATE_SWIM_LEFT : PLAYER_STATE_SWIM_RIGHT;
		} else {
			if (is_moving_x) {
				current_state = player->flip ? PLAYER_STATE_WALK_LEFT : PLAYER_STATE_WALK_RIGHT;
			} else {
				current_state = player->flip ? PLAYER_STATE_IDLE_LEFT : PLAYER_STATE_IDLE_RIGHT;
			}
		}

		if (state[SDL_SCANCODE_1]) {
			player->weapon_index = 1;
		} else if (state[SDL_SCANCODE_2]) {
			player->weapon_index = 2;
		} else if (state[SDL_SCANCODE_3]) {
			player->weapon_index = 3;
		} else if (state[SDL_SCANCODE_4]) {
			player->weapon_index = 4;
		} else if (state[SDL_SCANCODE_5]) {
			player->weapon_index = 5;
		} else if (state[SDL_SCANCODE_6]) {
			player->weapon_index = 6;
		} else if (state[SDL_SCANCODE_7]) {
			player->weapon_index = 7;
		} else if (state[SDL_SCANCODE_8]) {
			player->weapon_index = 8;
		} else if (state[SDL_SCANCODE_K]) {
			emp_create_level(G->active_level, 1);
		}
	}

	emp_texture_t* tex = NULL;
	switch (current_state) {
	case PLAYER_STATE_IDLE_LEFT:
	case PLAYER_STATE_IDLE_RIGHT:
		tex = G->assets->png->bird_idle_32.handle;
		break;
	case PLAYER_STATE_SWIM_LEFT:
	case PLAYER_STATE_SWIM_RIGHT:
		tex = G->assets->png->bird_swim_32.handle;
		break;
	case PLAYER_STATE_WALK_LEFT:
	case PLAYER_STATE_WALK_RIGHT:
		tex = G->assets->png->bird_walk_32.handle;
		break;
	}
	SDL_FRect src = source_rect(tex);

	SDL_FRect dst = player_rect(player->pos, tex);
	if (player->alive) {
		dst.x = player->flip ? dst.x + dst.w : dst.x;
		dst.w = player->flip ? -dst.w : dst.w;
		double t = 0.3;
		double has_taken_damage = player->last_damage_time + t - G->args->global_time;
		if (has_taken_damage > 0.0) {
			u8 mod_value = 255 - (u8)(600.0 * has_taken_damage);
			SDL_SetTextureColorMod(tex->texture, 255, mod_value, mod_value);
			SDL_RenderTexture(G->renderer, tex->texture, &src, &dst);
			SDL_SetTextureColorMod(tex->texture, 255, 255, 255);
		} else {
			SDL_SetTextureColorMod(tex->texture, 255, 255, 255);
			SDL_RenderTexture(G->renderer, tex->texture, &src, &dst);
		}

		player->bubble_timer -= dt;
		if (player->bubble_timer <= 0.0f) {
			player->bubble_timer = random_float(1.5f, 3.5f);
			int count = 2 + (simple_rng(&rng_state) % 5);
			float head_x = player->pos.x + (player->flip ? -3.0f : 3.0f);
			float head_y = player->pos.y - 12.0f;

			for (int i = 0; i < count; ++i) {
				emp_bullet_h bh = emp_create_bullet();
				if (bh.index != 0) {
					emp_bullet_t* b = &G->bullets[bh.index];
					b->pos.x = head_x + random_float(-2.0f, 2.0f);
					b->pos.y = head_y + random_float(-2.0f, 2.0f);

					b->vel.x = random_float(-8.0f, 8.0f);
					b->vel.y = -random_float(15.0f, 55.0f);
					b->life_left = random_float(1.0f, 1.6f);
					b->damage = 0.0f;
					b->texture_asset = &G->assets->png->bullet4_8;
					b->mask = 0;
					b->custom_render = NULL;
				}
			}
		}

		{
			emp_vec2_t mouse_pos;
			SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouse_pos.x, &mouse_pos.y);

			SDL_FRect shot_rect = player_rect(player->pos, tex);
			if (buttons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT) || state[SDL_SCANCODE_SPACE]) {
				if (player->last_shot + weapons[player->weapon_index]->delay_between_shots < G->args->global_time) {
					emp_vec2_t player_screen_pos = (emp_vec2_t) { .x = shot_rect.x + shot_rect.w / 2, .y = shot_rect.y + shot_rect.h / 2 };
					emp_vec2_t delta = emp_vec2_sub(mouse_pos, player_screen_pos);
					spawn_bullets(player->pos, delta, emp_enemy_bullet_mask | emp_heavy_bullet_mask, weapons[player->weapon_index]);
					player->last_shot = G->args->global_time;
				}
			}
		}

	} else {
		dst.y = dst.y + dst.h;
		dst.h = -dst.w;
		SDL_SetTextureColorMod(tex->texture, 47, 77, 47);
		SDL_RenderTexture(G->renderer, tex->texture, &src, &dst);

		double time_left = player->died_at_time + 3 - G->args->global_time;
		char buf[64];
		SDL_snprintf(buf, 64, "You died.. Respawn in %d", (int)time_left);
		emp_draw_text(dst.x - 230, dst.y, buf, 223, 132, 165, &G->assets->ttf->asepritefont);
		if (time_left <= 0) {
			emp_create_level(G->active_level, 1);
		}
	}

	{
		char buf[64];
		SDL_snprintf(buf, 64, "%d", (int)player->timer);
		emp_draw_text(16.0f, 64.0f, buf, 223, 132, 165, &G->assets->ttf->asepritefont);
	}
}

void emp_enemy_update(emp_enemy_t* enemy)
{
	float dist = emp_vec2_dist(G->player->pos, enemy->pos);
	if (dist > 450.0f) {
		return;
	}

	if (enemy->update) {
		enemy->update(enemy);
	}

	add_enemy_to_tile(enemy);

	emp_vec2_t player_pos = G->player->pos;
	emp_vec2_t dir = emp_vec2_normalize(emp_vec2_sub(player_pos, enemy->pos));

	if (enemy->last_shot + enemy->weapon->delay_between_shots * enemy->enemy_shot_delay <= G->args->global_time) {
		spawn_bullets(enemy->pos, dir, emp_player_bullet_mask, enemy->weapon);
		enemy->last_shot = G->args->global_time;
	}

	emp_texture_t* texture = enemy->texture_asset->handle;

	SDL_FRect src = source_rect(texture);

	emp_vec2_t pos = enemy->pos;
	pos.x = pos.x + (src.w * 0.25f);
	pos.y = pos.y + (src.h * 0.25f);

	SDL_FRect dst = render_rect(pos, texture);
	dst.x = enemy->flip ? dst.x + dst.w : dst.x;
	dst.w = enemy->flip ? -dst.w : dst.w;

	double t = 0.3;
	double has_taken_damage = enemy->last_damage_time + t - G->args->global_time;
	if (has_taken_damage > 0.0) {
		u8 mod_value = 255 - (u8)(600.0 * has_taken_damage);
		SDL_SetTextureColorMod(texture->texture, 255, mod_value, mod_value);
		SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);
		SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
	} else {
		SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);
	}
}

void emp_enemy_late_update(emp_enemy_t* enemy)
{
	if (enemy->late_update) {
		enemy->late_update(enemy);
	}

	if (enemy->health <= 0) {
		u32 spawned_by = enemy->spawned_by.index;
		enemy->alive = false;
		if (spawned_by && spawned_by < EMP_MAX_SPAWNERS) {
			emp_spawner_t* spawner = G->spawners + enemy->spawned_by.index;
			SDL_assert(spawner->count != 0);
			spawner->count = spawner->count - 1;
		}
	}
}

void emp_bullet_update(emp_bullet_t* bullet)
{
	bullet->life_left -= G->args->dt;

	bullet->pos.x += bullet->vel.x * G->args->dt;
	bullet->pos.y += bullet->vel.y * G->args->dt;

	if (bullet->life_left <= 0.0f) {
		bullet->alive = false;
	}

	emp_vec2i_t tile = get_tile(bullet->pos);

	if (tile_in_bounds(tile) && (bullet->mask & emp_particle_bullet_mask) == 0) {
		u32 index = ((u32)tile.y * EMP_LEVEL_WIDTH) + (u32)tile.x;
		emp_tile_t* tile_data = &G->level->tiles[index];

		if (tile_data->state != emp_tile_state_none) {
			bullet->alive = false;
			if (tile_data->state == emp_tile_state_breakable) {
				if (bullet->mask & emp_heavy_bullet_mask && G->level->health[index].value > 0) {
					G->level->health[index].value--;
					if (G->level->health[index].value == 0) {
						play_one_shot(&G->assets->ogg->obj_break);
					} else {
						play_one_shot(&G->assets->ogg->obj_damage);
					}
				}
			}
		}
	}

	if (bullet->custom_render) {
		bullet->custom_render(bullet);
	}

	if ((bullet->mask & emp_particle_bullet_mask) == 0) {
		emp_texture_t* tex = bullet->texture_asset->handle;
		SDL_FRect dstRect = render_rect(bullet->pos, bullet->texture_asset->handle);
		SDL_RenderTexture(G->renderer, tex->texture, NULL, &dstRect);
		draw_rect_at(bullet->pos, 32, 255, 0, 0, 255);

		if (bullet->mask & emp_enemy_bullet_mask) {
			for (int y = -1; y <= 1; ++y) {
				for (int x = -1; x <= 1; ++x) {
					emp_vec2i_t bullet_tile = get_tile(bullet->pos);
					if (tile_in_bounds(bullet_tile)) {
						const u64 index = index_from_tile(bullet_tile);
						if (index >= EMP_LEVEL_WIDTH * EMP_LEVEL_HEIGHT) {
							continue;
						}
						bullet_tile.x += x;
						bullet_tile.y += y;
						emp_enemy_t* enemy_in_tile = G->level->enemy_in_tile[index];
						while (enemy_in_tile != NULL) {
							if (check_overlap_bullet_enemy(bullet, enemy_in_tile)) {
								bullet->alive = false;
								enemy_in_tile->health -= bullet->damage;
								enemy_in_tile->last_damage_time = G->args->global_time;
								play_one_shot(&G->assets->ogg->enemy_damage);
								emp_damage_number(enemy_in_tile->pos, (u32)bullet->damage);
								goto collision_done;
							}
							enemy_in_tile = enemy_in_tile->next_in_tile;
						}
					}
				}
			}

			for (u32 i = 0; i < EMP_MAX_SPAWNERS; ++i) {
				emp_spawner_t* spawner = &G->spawners[i];
				if (spawner->alive) {
					emp_vec2_t pos = (emp_vec2_t) { .x = spawner->x, .y = spawner->y };
					SDL_FRect dst = render_rect(pos, G->assets->png->cave2_32.handle);
					emp_vec2_t centre = (emp_vec2_t) { .x = pos.x + (dst.w / 2), .y = pos.y + (dst.h / 2) };
					if (check_overlap_bullet(bullet, centre, dst.w)) {
						spawner->health = spawner->health - bullet->damage;
						bullet->alive = false;
						play_one_shot(&G->assets->ogg->enemy_damage);
						if (spawner->health == 0) {
							spawner->alive = false;
						}
					}
				}
			}
		}

	collision_done:;
		if (bullet->mask & emp_player_bullet_mask) {
			if (check_overlap_bullet_player(bullet, G->player->pos)) {
				// emp_damage_number(G->player->pos, (u32)bullet->damage);
				play_one_shot(&G->assets->ogg->player_damage);
				G->player->health = G->player->health -= bullet->damage;
				G->player->last_damage_time = G->args->global_time;
				bullet->alive = false;
			}
		}
	}
}

static emp_texture_t* emp_texture_find(const char* path)
{
	if (path == NULL) {
		return NULL;
	}
	// Maybe I should have just used a fucking hashmap at some point. Whoops
	if (SDL_strstr(G->assets->png->tilemap.path, path)) {
		return (emp_texture_t*)G->assets->png->tilemap.handle;
	}
	if (SDL_strstr(G->assets->png->presentation_sides.path, path)) {
		return (emp_texture_t*)G->assets->png->presentation_sides.handle;
	}
	if (SDL_strstr(G->assets->png->presentation.path, path)) {
		return (emp_texture_t*)G->assets->png->presentation.handle;
	}
	if (SDL_strstr(G->assets->png->env2_32.path, path)) {
		return (emp_texture_t*)G->assets->png->env2_32.handle;
	}
	if (SDL_strstr(G->assets->png->env2_32.path, path)) {
		return (emp_texture_t*)G->assets->png->env2_32.handle;
	}
	if (SDL_strstr(G->assets->png->env2_32.path, path)) {
		return (emp_texture_t*)G->assets->png->env2_32.handle;
	}
	if (SDL_strstr(G->assets->png->items_sheet8x8.path, path)) {
		return (emp_texture_t*)G->assets->png->items_sheet8x8.handle;
	}
	if (SDL_strstr(G->assets->png->items_sheet_12x12.path, path)) {
		return (emp_texture_t*)G->assets->png->items_sheet_12x12.handle;
	}
	if (SDL_strstr(G->assets->png->items_sheet16x16.path, path)) {
		return (emp_texture_t*)G->assets->png->items_sheet16x16.handle;
	}
	return NULL;
}

static void emp_entity_tilemap(emp_decoration_data_t* const level_data, emp_vec2_t offset)
{
	float grid_size = level_data->grid_size;
	emp_texture_t* deco = emp_texture_find(level_data->tiles.tilemap);
	if (deco == NULL) {
		return;
	}
	for (u64 ti = 0; ti < level_data->tiles.count; ti++) {
		emp_tile_desc_t* desc = level_data->tiles.values + ti;
		// SDL_FRect animated = source_rect(deco);
		SDL_FRect src = { desc->src.x, (float)desc->src.y, grid_size, grid_size };
		// Animations turned off
		// float value = (float)G->args->global_time / DECO_ANIMATION_SPEED;
		// u32 src_x = (u32)value % deco->columns;
		// src.x = (float)src_x * (float)level_data->grid_size;

		emp_vec2_t pos = emp_vec2_add(desc->dst, offset);

		pos.x = pos.x + grid_size * 0.5f;
		pos.y = pos.y + grid_size * 0.5f;
		pos.x = pos.x + level_data->offset_x;
		pos.y = pos.y + level_data->offset_y;
		SDL_FRect dst = render_rect_tile(pos, (float)level_data->grid_size);
		SDL_RenderTexture(G->renderer, deco->texture, &src, &dst);
	}
}

void emp_level_update(void)
{
	emp_level_asset_t* level_asset = (emp_level_asset_t*)G->active_level->handle;

	memset(G->level->tiles, 0, sizeof(*G->level->tiles) * EMP_LEVEL_TILES);

	for (u64 li = 0; li < level_asset->sublevels.count; li++) {
		emp_sublevel_t* sublevel = level_asset->sublevels.entries + li;

		emp_entity_tilemap(&sublevel->background, sublevel->offset);

		emp_texture_t* texture = emp_texture_find(sublevel->tiles.tilemap);
		if (texture != NULL) {
			for (u64 ti = 0; ti < sublevel->tiles.count; ti++) {
				float grid_size = sublevel->values.grid_size;
				emp_tile_desc_t* desc = sublevel->tiles.values + ti;
				u64 lx = (u64)(desc->dst.x / grid_size);
				u64 ly = (u64)(desc->dst.y / grid_size);

				size_t index = (size_t)(ly * sublevel->values.grid_width) + (size_t)lx;
				u8 value = sublevel->values.entries[index];

				SDL_FRect src = { desc->src.x, desc->src.y, grid_size, grid_size };
				emp_vec2_t pos = emp_vec2_add(desc->dst, sublevel->offset);
				u64 wx = (u64)(pos.x / grid_size);
				u64 wy = (u64)(pos.y / grid_size);

				u64 di = (wy * EMP_LEVEL_WIDTH) + wx;
				emp_tile_t* tile = &G->level->tiles[di];
				emp_tile_health_t* health = &G->level->health[di];

				if (value == 2) {
					if (health->value == 0) {
						continue;
					}
				}
				pos.x = pos.x + grid_size * 0.5f;
				pos.y = pos.y + grid_size * 0.5f;

				SDL_FRect dst = render_rect_tile(pos, grid_size);
				SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);

				if (value == 1) {
					tile->state = emp_tile_state_occupied;
					draw_rect_at(pos, grid_size, 255, 0, 0, 255);
				}
				if (value == 2) {
					tile->state = emp_tile_state_breakable;
					draw_rect_at(pos, grid_size, 255, 255, 0, 255);
				}
			}
		}

		emp_entity_tilemap(&sublevel->extra, sublevel->offset);

		emp_entity_tilemap(&sublevel->core, sublevel->offset);

		emp_entity_tilemap(&sublevel->items_8x8, sublevel->offset);
		emp_entity_tilemap(&sublevel->items_12x12, sublevel->offset);
		emp_entity_tilemap(&sublevel->items_16x16, sublevel->offset);
	}
	SDL_memset(G->level->enemy_in_tile, 0, sizeof(emp_enemy_t*) * EMP_LEVEL_TILES);
}

void emp_generator_uptdate(emp_bullet_generator_t* generator)
{
}

void emp_ka_ching(emp_vec2_t pos)
{
	spawn_bullets(pos, (emp_vec2_t) { 1.0f, 1.0f }, 0, particle_config);
}

void emp_damage_number(emp_vec2_t pos, u32 number)
{
	text_particle_config->shots[0].damage = (float)number;
	text_particle_config->shots[0].start_angle = random_float(-25.0, 25);
	spawn_bullets(pos, (emp_vec2_t) { 0.0f, -1.0f }, emp_particle_bullet_mask, text_particle_config);
}

void emp_entities_init()
{
	G->player = SDL_malloc(sizeof(emp_player_t) * EMP_MAX_PLAYERS);
	G->enemies = SDL_malloc(sizeof(emp_enemy_t) * EMP_MAX_ENEMIES);
	G->bullets = SDL_malloc(sizeof(emp_bullet_t) * EMP_MAX_BULLETS);
	G->generators = SDL_malloc(sizeof(emp_bullet_generator_t) * EMP_MAX_BULLET_GENERATORS);
	G->spawners = SDL_malloc(sizeof(emp_spawner_t) * EMP_MAX_SPAWNERS);

	SDL_memset(G->player, 0, sizeof(emp_player_t) * EMP_MAX_PLAYERS);
	SDL_memset(G->enemies, 0, sizeof(emp_enemy_t) * EMP_MAX_ENEMIES);
	SDL_memset(G->bullets, 0, sizeof(emp_bullet_t) * EMP_MAX_BULLETS);
	SDL_memset(G->generators, 0, sizeof(emp_bullet_generator_t) * EMP_MAX_BULLET_GENERATORS);
	SDL_memset(G->spawners, 0, sizeof(emp_spawner_t) * EMP_MAX_SPAWNERS);
}

int emp_teleporter_uptdate(emp_level_teleporter_t const* teleporter, bool* is_thinking_about_it)
{
	emp_asset_t* texture_asset = &G->assets->png->cave_32;
	emp_texture_t* texture = texture_asset->handle;
	SDL_FRect src = source_rect(texture);
	emp_vec2_t pos = (emp_vec2_t) {
		.x = teleporter->x - (float)EMP_TILE_SIZE / 2,
		.y = teleporter->y - (float)EMP_TILE_SIZE / 2,
	};
	pos.y = pos.y + (teleporter->h * 0.25f);
	pos.x = pos.x + (teleporter->w * 0.25f);
	SDL_FRect dst = render_rect(pos, texture);

	emp_vec2_t centre = (emp_vec2_t) { .x = dst.x + (dst.w / 2), .y = dst.y + (dst.h / 2) };

	int on_teleporter = 0;
	float distance = emp_vec2_dist(G->player->pos, pos);

	emp_vec2_t mouse_pos;
	SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouse_pos.x, &mouse_pos.y);
	int is_hovering = emp_vec2_dist(mouse_pos, centre) < EMP_TILE_SIZE * SPRITE_MAGNIFICATION;

	if (distance < EMP_TILE_SIZE) {
		if (is_hovering) {
			float ex = dst.w * 0.25f;
			float ey = dst.h * 0.25f;
			dst.w = dst.w + ex;
			dst.h = dst.h + ey;
			dst.x = dst.x - (ex * 0.5f);
			dst.y = dst.y - (ex * 0.5f);
			*is_thinking_about_it = true;
		}
		if (buttons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT) && is_hovering) {
			if (G->player->is_teleporting == 0 /*&& G->player->was_teleporting == 0*/) {
				emp_level_asset_t* level = (emp_level_asset_t*)G->active_level->handle;
				u32 found = emp_level_teleporter_list_find(&level->teleporters, teleporter->other);
				if (found) {
					u32 at = found - 1;
					emp_level_teleporter_t const* tp = level->teleporters.entries + at;

					G->player->pos.x = tp->x - (EMP_TILE_SIZE / 2.0f);
					G->player->pos.y = tp->y - (EMP_TILE_SIZE / 2.0f);
					G->player->is_teleporting = 1;
					play_one_shot(&G->assets->ogg->teleport);

					emp_ka_ching(G->player->pos);
				}
			}

			G->player->is_teleporting = 1;
			on_teleporter = G->player->is_teleporting;
		}
	}

	if (G->player->is_teleporting) {

		SDL_SetTextureAlphaMod(texture->texture, 64);
		SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);
		SDL_SetTextureAlphaMod(texture->texture, 255);
	} else {
		SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);
	}

	draw_rect_at(pos, teleporter->w, 255, 0, 0, 255);
	return on_teleporter;
}

void emp_spawner_update(u32 index, emp_spawner_t* spawner)
{
	emp_vec2_t pos = (emp_vec2_t) { .x = spawner->x, .y = spawner->y };
	float dist = emp_vec2_dist(G->player->pos, pos);
	if (dist > 450.0f) {
		return;
	}
	if (spawner->count < spawner->limit) {
		spawner->accumulator = spawner->accumulator - G->args->dt;
		if (spawner->accumulator <= 0.0f) {
			spawner->accumulator = spawner->accumulator + spawner->frequency;
			spawner->count = spawner->count + 1;

			emp_create_enemy(pos, spawner->enemy_conf_index, spawner->enemy_health, spawner->movement_speed, spawner->weapon_index, (emp_spawner_h) { .index = index });
		}
	}

	emp_asset_t* texture_asset = &G->assets->png->cave2_32;
	emp_texture_t* texture = texture_asset->handle;
	SDL_FRect src = source_rect(texture);
	SDL_FRect dst = render_rect(pos, texture);
	SDL_RenderTexture(G->renderer, texture->texture, &src, &dst);
}

void emp_entities_update()
{
	emp_level_update();
	emp_music_player_update(G->music_player);

	bool is_hovering_teleporter = false;
	int is_teleporting = 0;
	emp_level_asset_t* level = (emp_level_asset_t*)G->active_level->handle;
	for (u64 i = 0; i < SDL_arraysize(level->teleporters.entries); i++) {
		emp_level_teleporter_t* tp = level->teleporters.entries + i;
		if (tp->id != 0) {
			is_teleporting = emp_teleporter_uptdate(tp, &is_hovering_teleporter) || is_teleporting;
		}
	}
	G->player->is_teleporting = is_teleporting;

	if (is_hovering_teleporter) {
		emp_hover_cursor();
	} else {
		emp_idle_cursor();
	}

	for (u64 i = 0; i < EMP_MAX_PLAYERS; ++i) {
		emp_player_t* player = &G->player[i];
		emp_player_update(player);
	}

	for (u64 i = 0; i < EMP_MAX_ENEMIES; ++i) {
		emp_enemy_t* enemy = &G->enemies[i];
		if (enemy->alive) {
			emp_enemy_update(enemy);
		}
	}

	for (u64 i = 0; i < EMP_MAX_BULLETS; ++i) {
		emp_bullet_t* bullet = &G->bullets[i];
		if (bullet->alive) {
			emp_bullet_update(bullet);
		}
	}

	for (u32 i = 0; i < EMP_MAX_SPAWNERS; ++i) {
		emp_spawner_t* spawner = &G->spawners[i];
		if (spawner->alive) {
			emp_spawner_update(i, spawner);
		}
	}

	for (u64 i = 0; i < EMP_MAX_BULLET_GENERATORS; ++i) {
		emp_bullet_generator_t* generator = &G->generators[i];
		if (generator->alive) {
			emp_generator_uptdate(generator);
		}
	}

	//  LATE UPDATES

	for (u64 i = 0; i < EMP_MAX_ENEMIES; ++i) {
		emp_enemy_t* enemy = &G->enemies[i];
		if (enemy->alive) {
			emp_enemy_late_update(enemy);
		}
	}
}

void setup_level(emp_asset_t* level_asset)
{
	SDL_memset(G->enemies, 0, sizeof(emp_enemy_t) * EMP_MAX_ENEMIES);
	SDL_memset(G->bullets, 0, sizeof(emp_bullet_t) * EMP_MAX_BULLETS);
	SDL_memset(G->generators, 0, sizeof(emp_bullet_generator_t) * EMP_MAX_BULLET_GENERATORS);
	SDL_memset(G->spawners, 0, sizeof(emp_spawner_t) * EMP_MAX_SPAWNERS);

	u32 player = emp_create_player();

	emp_level_asset_t* level = (emp_level_asset_t*)level_asset->handle;
	u32 found = emp_level_query(level, emp_entity_type_player, 0);
	if (found) {
		emp_level_entity_t* player_entity = emp_level_get(level, found - 1);
		float half = level->entities.grid_size * 0.5f;
		float x = player_entity->x - half;
		float y = player_entity->y - half;
		G->player[player].pos.x = x;
		G->player[player].pos.y = y;
		G->player[player].movement_speed = player_entity->movement_speed;
	} else {
		SDL_Log("No Player config broke!");
	}

	found = 0;
	for (;;) {
		found = emp_level_query(level, emp_entity_type_spawner, found);
		if (!found) {
			break;
		}

		emp_level_entity_t* spawner = emp_level_get(level, found - 1);
		emp_vec2_t pos = (emp_vec2_t) {
			.x = spawner->x - (float)EMP_TILE_SIZE / 2,
			.y = spawner->y - (float)EMP_TILE_SIZE / 2,
		};
		switch (spawner->behaviour) {
		case emp_behaviour_type_roamer:
			emp_create_spawner(pos, spawner->health, spawner->enemy_health, spawner->movement_speed, 0, spawner->weapon_index, spawner->frequency, spawner->limit);
			break;
		case emp_behaviour_type_chaser:
			emp_create_spawner(pos, spawner->health, spawner->enemy_health, spawner->movement_speed, 2, spawner->weapon_index, spawner->frequency, spawner->limit);
			break;
		default:
			break;
		}
	}

	found = 0;
	for (;;) {
		found = emp_level_query(level, emp_entity_type_boss, found);
		if (!found) {
			break;
		}

		float half = level->entities.grid_size * 0.5f;
		emp_level_entity_t* boss = emp_level_get(level, found - 1);
		float x = boss->x - half;
		float y = boss->y - half;

		switch (boss->behaviour) {
		case emp_behaviour_type_roamer:
			emp_create_enemy((emp_vec2_t) { x, y }, 1, 0.0f, boss->movement_speed, boss->weapon_index, (emp_spawner_h) { .index = EMP_MAX_SPAWNERS });
			break;
		case emp_behaviour_type_chaser:
			emp_create_enemy((emp_vec2_t) { x, y }, 3, 0.0f, boss->movement_speed, boss->weapon_index, (emp_spawner_h) { .index = EMP_MAX_SPAWNERS });
			break;
		default:
			break;
		}
	}

	found = 0;
	for (;;) {
		found = emp_level_query(level, emp_entity_type_chest, found);
		if (!found) {
			break;
		}

		float half = level->entities.grid_size * 0.5f;
		emp_level_entity_t* chest = emp_level_get(level, found - 1);
		float x = chest->x - half;
		float y = chest->y - half;
		emp_create_chest((emp_vec2_t) { x, y }, chest->weapon_index);
	}

	for (u64 li = 0; li < level->sublevels.count; li++) {
		emp_sublevel_t* sublevel = level->sublevels.entries + li;
		for (u64 ti = 0; ti < sublevel->tiles.count; ti++) {
			float grid_size = sublevel->values.grid_size;
			emp_tile_desc_t* desc = sublevel->tiles.values + ti;
			u64 lx = (u64)(desc->dst.x / grid_size);
			u64 ly = (u64)(desc->dst.y / grid_size);

			size_t index = (size_t)(ly * sublevel->values.grid_width) + (size_t)lx;
			u8 value = sublevel->values.entries[index];

			emp_vec2_t pos = emp_vec2_add(desc->dst, sublevel->offset);
			u64 wx = (u64)(pos.x / grid_size);
			u64 wy = (u64)(pos.y / grid_size);

			u64 di = (wy * EMP_LEVEL_WIDTH) + wx;
			emp_tile_health_t* health = &G->level->health[di];
			if (value == 2) {
				health->value = 2;
			}
		}
	}
}

void emp_create_level(emp_asset_t* level_asset, int is_reload)
{
	G->active_level = level_asset;

	if (!is_reload) {
		G->level = SDL_malloc(sizeof(emp_level_t));
		G->level->tiles = SDL_malloc(sizeof(*G->level->tiles) * EMP_LEVEL_TILES);
		G->level->health = SDL_malloc(sizeof(*G->level->health) * EMP_LEVEL_TILES);
		G->level->enemy_in_tile = SDL_malloc(sizeof(emp_enemy_t*) * EMP_LEVEL_TILES);
	}
	emp_tile_t* tiles = G->level->tiles;
	emp_tile_health_t* health = G->level->health;
	emp_enemy_t** enemy_in_tile = G->level->enemy_in_tile;
	SDL_memset(G->level->tiles, 0, sizeof(*G->level->tiles) * EMP_LEVEL_TILES);
	SDL_memset(G->level->health, 0, sizeof(*G->level->health) * EMP_LEVEL_TILES);
	SDL_memset(G->level->enemy_in_tile, 0, sizeof(emp_enemy_t*) * EMP_LEVEL_TILES);
	SDL_zerop(G->level);
	G->level->tiles = tiles;
	G->level->health = health;
	G->level->enemy_in_tile = enemy_in_tile;
	SDL_Log("%s", level_asset->path);
	setup_level(level_asset);

	g_idle_cursor = CreateCyanBlueCursor(0xFFE6FAFC, 0xFF8CF2E6, 0xFF46D1DB, 0xFF2196CA, 0xFF1464A0);
	g_hovered_cursor = CreateCyanBlueCursor(0xFFFFFFFF, 0xFFD6FFFF, 0xFF78FFE8, 0xFF30E6CC, 0xFF14B4A4);
	emp_idle_cursor();
}

void emp_destroy_level(void)
{
	SDL_free(G->level->tiles);
	SDL_free(G->level);

	if (g_idle_cursor)
		SDL_DestroyCursor(g_idle_cursor);
	if (g_hovered_cursor)
		SDL_DestroyCursor(g_hovered_cursor);
}