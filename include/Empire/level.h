#pragma once
#include "Empire/types.h"
struct emp_asset_t;

#define emp_sublevel_capacity 128
#define emp_value_buffer_capacity 512 * 512
#define emp_entities_capacity 2048
#define emp_teleporter_capacity 32

typedef struct emp_value_buffer_t
{
	u8 values[emp_value_buffer_capacity];
	u32 count;
} emp_value_buffer_t;

typedef struct emp_tile_desc_t
{
	emp_vec2_t src;
	emp_vec2_t dst;
} emp_tile_desc_t;

typedef struct emp_value_grid_t
{
	float grid_size;
	u32 grid_width;
	u32 grid_height;
	u8* entries;
	u32 count;
} emp_value_grid_t;

typedef struct emp_tiles_data_t
{
	char* tilemap;
	emp_tile_desc_t* values;
	u32 count;
} emp_tiles_data_t;

typedef struct emp_decoration_data_t
{
	float grid_size;
	float offset_x;
	float offset_y;
	emp_tiles_data_t tiles;
} emp_decoration_data_t;

typedef struct emp_sublevel_t
{
	emp_vec2_t offset;

	emp_tiles_data_t tiles;
	emp_value_grid_t values;

	emp_decoration_data_t items_8x8;
	emp_decoration_data_t items_12x12;
	emp_decoration_data_t items_16x16;

	emp_decoration_data_t core;
	emp_decoration_data_t extra;
	emp_decoration_data_t background;
} emp_sublevel_t;

typedef struct emp_sublevel_list_t
{
	emp_sublevel_t entries[emp_sublevel_capacity];
	u32 count;
} emp_sublevel_list_t;

typedef struct emp_tile_desc_list_t
{
	emp_tile_desc_t entries[emp_value_buffer_capacity];
	u32 count;
} emp_tile_desc_list_t;

typedef enum emp_entity_type {
	emp_entity_type_player,
	emp_entity_type_spawner,
	emp_entity_type_boss,
	emp_entity_type_chest,
} emp_entity_type;

typedef enum emp_behaviour_type {
	emp_behaviour_type_none,
	emp_behaviour_type_roamer,
	emp_behaviour_type_chaser,
} emp_behaviour_type;

typedef struct emp_level_entity_t
{
	emp_entity_type type;
	emp_behaviour_type behaviour;
	u32 weapon_index;
	float x;
	float y;
	float w;
	float h;

	float frequency;
	float enemy_health;
	float health;
	float movement_speed;
	u32 limit;
} emp_level_entity_t;

typedef struct emp_level_entities_list_t
{
	float grid_size;

	emp_level_entity_t entries[emp_entities_capacity];
	u32 count;
} emp_level_entities_list_t;

typedef struct emp_level_teleporter_t
{
	u64 id;

	float x;
	float y;
	float w;
	float h;

	u64 other;
} emp_level_teleporter_t;

typedef struct emp_level_teleporter_list_t
{
	emp_level_teleporter_t entries[emp_teleporter_capacity];
	u32 count;
} emp_level_teleporter_list_t;

typedef struct emp_level_asset_t
{
	emp_tile_desc_list_t tiles;
	emp_value_buffer_t values;
	emp_sublevel_list_t sublevels;
	emp_level_entities_list_t entities;
	emp_level_teleporter_list_t teleporters;
} emp_level_asset_t;

u32 emp_level_query(emp_level_asset_t* level, emp_entity_type type, u32 offset);
emp_level_entity_t* emp_level_get(emp_level_asset_t* level, u32 at);

u32 emp_level_teleporter_list_find(emp_level_teleporter_list_t* set, u64 id);

void emp_load_level_asset(struct emp_asset_t* asset);
void emp_unload_level_asset(struct emp_asset_t* asset);
