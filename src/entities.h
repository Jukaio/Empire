#pragma once

#include <Empire/types.h>
#include <Empire/miniaudio.h>

extern float SPRITE_MAGNIFICATION;

typedef struct emp_asset_t emp_asset_t;
typedef struct emp_enemy_t emp_enemy_t;
typedef struct emp_bullet_t emp_bullet_t;

typedef void (*emp_enemy_update_f)(emp_enemy_t*);
typedef void (*emp_bullet_render_f)(emp_bullet_t*);

typedef struct emp_enemy_h
{
	u32 index;
	u32 generation;
} emp_enemy_h;

typedef struct emp_bullet_h
{
	u32 index;
	u32 generation;
} emp_bullet_h;

typedef struct emp_spawner_h
{
	u32 index;
} emp_spawner_h;

typedef struct emp_bullet_generator_h
{
	u32 index;
	u32 generation;
} emp_bullet_generator_h;

typedef struct emp_bullet_conf_t
{
	float speed;
	float start_angle;
	float lifetime;
	float damage;
	emp_asset_t* texture_asset;
	emp_bullet_render_f custom_render;
} emp_bullet_conf_t;

typedef struct emp_weapon_conf_t
{
	float delay_between_shots;
	u32 num_shots;
	emp_bullet_conf_t shots[36];
	emp_asset_t* sound_asset;
	double last_played_ms;
} emp_weapon_conf_t;

typedef struct emp_enemy_conf_t
{
	float health;
	float speed;
	emp_asset_t* texture_asset;
	emp_enemy_update_f update;
	emp_enemy_update_f late_update;
	u32 data_size;
} emp_enemy_conf_t;

#define EMP_MAX_PLAYERS 1
typedef struct emp_player_t
{
	u32 generation;
	u32 weapon_index;
	emp_vec2_t pos;
	emp_vec2_t vel;
	double last_shot;
	bool is_teleporting;
	bool flip;
	bool alive;
	bool prev_w_pressed;
	bool prev_s_pressed;
	bool prev_a_pressed;
	bool prev_d_pressed;
	float health;
	float max_health;
	double last_damage_time;
	double died_at_time;
	float movement_speed;
	int wants_debug;
	float timer;
} emp_player_t;

#define EMP_MAX_ENEMIES 256
typedef struct emp_enemy_t
{
	emp_enemy_t* next_in_tile;

	bool alive;
	u32 generation;
	float health;
	float speed;
	float enemy_shot_delay;
	emp_vec2_t pos;
	emp_vec2_t direction;
	emp_asset_t* texture_asset;
	emp_weapon_conf_t* weapon;
	emp_enemy_update_f update;
	emp_enemy_update_f late_update;
	double last_shot;
	double last_damage_time;
	float movement_speed;
	emp_spawner_h spawned_by;
	bool flip;
	u8 dynamic_data[64];
} emp_enemy_t;

#define EMP_MAX_SPAWNERS 32
typedef struct emp_spawner_t
{
	bool alive;
	float x;
	float y;
	float health;
	float enemy_health;
	float movement_speed;
	float accumulator;
	float frequency;
	u32 enemy_conf_index;
	u32 weapon_index;
	u32 limit;
	u32 count;
} emp_spawner_t;

typedef enum bullet_mask {
	emp_player_bullet_mask = 1 << 1,
	emp_enemy_bullet_mask = 1 << 2,
	emp_heavy_bullet_mask = 1 << 3,
	emp_particle_bullet_mask = 1 << 4,
} bullet_mask;

#define EMP_MAX_BULLETS 65535
typedef struct emp_bullet_t
{
	bool alive;
	u32 generation;
	emp_vec2_t pos;
	emp_vec2_t vel;
	float life_left;
	float damage;
	emp_asset_t* texture_asset;
	bullet_mask mask;
	emp_bullet_render_f custom_render;
} emp_bullet_t;

#define EMP_MAX_BULLET_GENERATORS 1024
typedef struct emp_bullet_generator_t
{
	bool alive;
	u32 generation;
	emp_weapon_conf_t weapons[16];
} emp_bullet_generator_t;

typedef enum emp_tile_state {
	emp_tile_state_none,
	emp_tile_state_occupied,
	emp_tile_state_breakable
} emp_tile_state;

typedef struct emp_tile_t
{
	emp_tile_state state;
	emp_enemy_h first_in_tile;
} emp_tile_t;

typedef struct emp_tile_health_t
{
	u8 value;
} emp_tile_health_t;

#define EMP_TILE_SIZE 16.0f
#define EMP_LEVEL_WIDTH 1024
#define EMP_LEVEL_HEIGHT 1024
#define EMP_LEVEL_TILES EMP_LEVEL_WIDTH * EMP_LEVEL_HEIGHT

typedef struct emp_level_t
{
	emp_tile_t* tiles;
	emp_enemy_t** enemy_in_tile;
	emp_tile_health_t* health;
} emp_level_t;

typedef struct emp_music_player emp_music_player;

typedef struct emp_entities_t
{
	SDL_Renderer* renderer;
	emp_update_args_t* args;
	emp_generated_assets_o* assets;
	emp_player_t* player;
	emp_enemy_t* enemies;
	emp_bullet_t* bullets;
	emp_spawner_t* spawners;
	emp_bullet_generator_t* generators;
	emp_level_t* level;
	emp_music_player* music_player;
	ma_engine* mixer;
	emp_asset_t* active_level;
} emp_G;

extern emp_G* G;

void emp_music_player_init(void);
void emp_init_enemy_configs();
void emp_init_weapon_configs();
u32 emp_create_player();


emp_bullet_generator_h emp_create_bullet_generator();
void emp_destroy_bullet_generator(emp_bullet_generator_h handle);

void emp_entities_init();
void emp_entities_update();

void emp_create_level(emp_asset_t* level_asset, int is_reload);
void emp_destroy_level(void);