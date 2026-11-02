
#include <Empire/assets.h>
#include <Empire/hash.inl>
#include <Empire/level.h>
#include <Empire/yyjson.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

emp_sublevel_t* emp_level_create_sublevel(emp_level_asset_t* level, emp_value_grid_t* values, emp_tiles_data_t* tiles, emp_decoration_data_t* core, emp_decoration_data_t* extra, emp_decoration_data_t* background, emp_decoration_data_t* i8x8, emp_decoration_data_t* i12x12, emp_decoration_data_t* i16x16)
{
	emp_sublevel_t* sublevel = level->sublevels.entries + level->sublevels.count;
	level->sublevels.count = level->sublevels.count + 1;
	sublevel->values = *values;
	sublevel->tiles = *tiles;
	sublevel->core = *core;
	sublevel->extra = *extra;
	sublevel->background = *background;
	sublevel->items_8x8 = *i8x8;
	sublevel->items_12x12 = *i12x12;
	sublevel->items_16x16 = *i16x16;
	return sublevel;
}

emp_value_grid_t emp_level_reserve_values(emp_level_asset_t* level, u32 count)
{
	u8* data = level->values.values + level->values.count;
	level->values.count = level->values.count + count;
	return (emp_value_grid_t) { .entries = data, .count = count };
}

emp_tiles_data_t emp_level_reserve_tiles(emp_level_asset_t* level, u32 count)
{
	emp_tile_desc_t* data = level->tiles.entries + level->tiles.count;
	level->tiles.count = level->tiles.count + count;
	return (emp_tiles_data_t) { .values = data, .count = count };
}

void emp_level_parse_int_grid_data(emp_level_asset_t* level, yyjson_val* layer, emp_value_grid_t* values)
{
	yyjson_val* int_grid = yyjson_obj_get(layer, "intGridCsv");

	u64 idx;
	u64 size = yyjson_arr_size(int_grid);
	*values = emp_level_reserve_values(level, (u32)size);

	yyjson_val* value;
	yyjson_arr_foreach(int_grid, idx, size, value)
	{
		values->entries[idx] = (u8)yyjson_get_int(value);
	}
}

void emp_level_parse_tile_data(emp_level_asset_t* level, const char* name, yyjson_val* layer, emp_tiles_data_t* tiles)
{
	yyjson_val* layer_tiles = yyjson_obj_get(layer, name);

	u64 idx, size;
	size = yyjson_arr_size(layer_tiles);
	*tiles = emp_level_reserve_tiles(level, (u32)size);

	yyjson_val* value;
	yyjson_arr_foreach(layer_tiles, idx, size, value)
	{
		emp_tile_desc_t* tile_desc = tiles->values + idx;
		yyjson_val* px = yyjson_obj_get(value, "px");
		yyjson_val* src = yyjson_obj_get(value, "src");

		tile_desc->dst.x = (float)yyjson_get_num(yyjson_arr_get(px, 0));
		tile_desc->dst.y = (float)yyjson_get_num(yyjson_arr_get(px, 1));
		tile_desc->src.x = (float)yyjson_get_num(yyjson_arr_get(src, 0));
		tile_desc->src.y = (float)yyjson_get_num(yyjson_arr_get(src, 1));
	}
}

int emp_level_add_entities_from_fields(emp_level_asset_t* level, yyjson_val* fields, emp_level_entity_t* out_entity)
{
	u64 idx, size;
	yyjson_val* field;
	int is_reasonably_constructed = 0;
	yyjson_arr_foreach(fields, idx, size, field)
	{
		yyjson_val* identifier = yyjson_obj_get(field, "__identifier");
		if (identifier) {
			const char* str = yyjson_get_str(identifier);
			if (SDL_strcmp(str, "weapon") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->weapon_index = (uint32_t)yyjson_get_uint(value);
					is_reasonably_constructed = 1;
				}
			}

			if (SDL_strcmp(str, "limit") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->limit = (uint32_t)yyjson_get_uint(value);
				}
			}

			if (SDL_strcmp(str, "frequency") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->frequency = (float)yyjson_get_num(value);
				}
			}

			if (SDL_strcmp(str, "health") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->health = (float)yyjson_get_num(value);
				}
			}

			if (SDL_strcmp(str, "enemy_health") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->enemy_health = (float)yyjson_get_num(value);
				}
			}

			if (SDL_strcmp(str, "movement_speed") == 0) {
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					out_entity->movement_speed = (float)yyjson_get_num(value);
				}
			}
		}

		yyjson_val* type = yyjson_obj_get(field, "__type");
		if (type) {
			const char* str = yyjson_get_str(type);
			if (SDL_strcmp(str, "LocalEnum.behaviour") == 0) {
				out_entity->type = emp_entity_type_spawner;
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					if (SDL_strcmp(yyjson_get_str(value), "roamer") == 0) {
						out_entity->behaviour = emp_behaviour_type_roamer;
						is_reasonably_constructed = 1;
					}
					if (SDL_strcmp(yyjson_get_str(value), "chaser") == 0) {
						out_entity->behaviour = emp_behaviour_type_chaser;
						is_reasonably_constructed = 1;
					}
				}
			}

			if (SDL_strcmp(str, "LocalEnum.boss") == 0) {
				out_entity->type = emp_entity_type_boss;
				yyjson_val* value = yyjson_obj_get(field, "__value");
				if (value) {
					if (SDL_strcmp(yyjson_get_str(value), "jelly") == 0) {
						out_entity->behaviour = emp_behaviour_type_roamer;
						is_reasonably_constructed = 1;
					}
					if (SDL_strcmp(yyjson_get_str(value), "octi") == 0) {
						out_entity->behaviour = emp_behaviour_type_chaser;
						is_reasonably_constructed = 1;
					}
				}
			}
		}
	}
	if (!is_reasonably_constructed) {
		SDL_Log("No Entity mapping found");
	}
	return is_reasonably_constructed;
}

static void emp_level_entities_list_add(emp_level_entities_list_t* list, emp_level_entity_t* value)
{
	if (list->count < SDL_arraysize(list->entries)) {
		list->entries[list->count] = *value;
		list->count = list->count + 1;
	} else {
		SDL_Log("No space in entity list anymore. Bump the capacity");
	}
}

static void emp_level_teleporter_list_add(emp_level_teleporter_list_t* set, emp_level_teleporter_t* value)
{
	u32 slot = value->id % SDL_arraysize(set->entries);
	for (;;) {
		emp_level_teleporter_t* teleporter = set->entries + slot;
		if (teleporter->id == 0) {
			set->count = set->count + 1;
			*teleporter = *value;
			return;
		}
		slot = (slot + 1) % SDL_arraysize(set->entries);
	}
}

u32 emp_level_teleporter_list_find(emp_level_teleporter_list_t* set, u64 id)
{
	u32 slot = id % SDL_arraysize(set->entries);
	for (;;) {
		emp_level_teleporter_t* teleporter = set->entries + slot;
		if (teleporter->id == 0) {
			return 0;
		}
		if (teleporter->id == id) {
			return slot + 1;
		}
		slot = (slot + 1) % SDL_arraysize(set->entries);
	}
}

static void emp_level_add_entities(emp_level_asset_t* level, yyjson_val* instances)
{
	u64 idx, size;
	yyjson_val* instance;
	yyjson_arr_foreach(instances, idx, size, instance)
	{
		emp_level_entity_t entity = { 0 };

		entity.x = (float)yyjson_get_num(yyjson_obj_get(instance, "__worldX"));
		entity.y = (float)yyjson_get_num(yyjson_obj_get(instance, "__worldY"));
		entity.w = (float)yyjson_get_num(yyjson_obj_get(instance, "width"));
		entity.h = (float)yyjson_get_num(yyjson_obj_get(instance, "height"));

		yyjson_val* identifier = yyjson_obj_get(instance, "__identifier");

		int is_reasonably_constructed = 0;

		if (identifier) {
			const char* str = yyjson_get_str(identifier);
			if (SDL_strcmp(str, "player") == 0) {
				entity.type = emp_entity_type_player;
				entity.behaviour = emp_behaviour_type_none;
				// emp_level_entities_list_add(&level->entities, &entity);
				is_reasonably_constructed = 1;
			}

			if (SDL_strcmp(str, "teleporter") == 0) {
				emp_level_teleporter_t tp = { 0 };
				tp.id = hash_str(yyjson_get_str(yyjson_obj_get(instance, "iid")));
				tp.x = entity.x;
				tp.y = entity.y;
				tp.w = entity.w;
				tp.h = entity.h;

				yyjson_val* fields = yyjson_obj_get(instance, "fieldInstances");
				yyjson_val* other = yyjson_arr_get_first(fields);
				yyjson_val* value = yyjson_obj_get(other, "__value");
				yyjson_val* eid = yyjson_obj_get(value, "entityIid");
				if (eid) {
					tp.other = hash_str(yyjson_get_str(eid));
				}
				// THIS ONE IS NOT GETTING ADDED
				emp_level_teleporter_list_add(&level->teleporters, &tp);
				continue;
			}

			if (SDL_strcmp(str, "chest") == 0) {
				entity.type = emp_entity_type_chest;
				entity.behaviour = emp_behaviour_type_none;
			}
		}

		yyjson_val* fields = yyjson_obj_get(instance, "fieldInstances");
		if (emp_level_add_entities_from_fields(level, fields, &entity)) {
			is_reasonably_constructed = 1;
		}
		if (is_reasonably_constructed) {
			emp_level_entities_list_add(&level->entities, &entity);
		}
	}
}

void emp_load_sublevel(emp_level_asset_t* level, u32 index, yyjson_val* data)
{
	yyjson_val* layerInstances = yyjson_obj_get(data, "layerInstances");

	u64 idx, size;

	yyjson_val* layer;

	emp_tiles_data_t tiles = { 0 };
	emp_value_grid_t values = { 0 };
	// emp_decoration_data_t decoration = { 0 };
	emp_decoration_data_t extra = { 0 };
	emp_decoration_data_t core = { 0 };
	emp_decoration_data_t background = { 0 };

	emp_decoration_data_t i8x8 = { 0 };
	emp_decoration_data_t i12x12 = { 0 };
	emp_decoration_data_t i16x16 = { 0 };

	yyjson_arr_foreach(layerInstances, idx, size, layer)
	{
		yyjson_val* id = yyjson_obj_get(layer, "__identifier");
		const char* str = yyjson_get_str(id);

		if (SDL_strcmp(str, "world") == 0) {
			emp_level_parse_int_grid_data(level, layer, &values);
			emp_level_parse_tile_data(level, "autoLayerTiles", layer, &tiles);
			tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			values.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			values.grid_width = (u32)yyjson_get_uint(yyjson_obj_get(layer, "__cWid"));
			values.grid_height = (u32)yyjson_get_uint(yyjson_obj_get(layer, "__cHei"));
			continue;
		}

		if (SDL_strcmp(str, "entities") == 0) {
			level->entities.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			yyjson_val* instances = yyjson_obj_get(layer, "entityInstances");
			emp_level_add_entities(level, instances);
			continue;
		}

		if (SDL_strcmp(str, "items8x8") == 0) {
			i8x8.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			i8x8.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			i8x8.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &i8x8.tiles);
			i8x8.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}

		if (SDL_strcmp(str, "items16x16") == 0) {
			i16x16.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			i16x16.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			i16x16.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &i16x16.tiles);
			i16x16.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}

		if (SDL_strcmp(str, "items12x12") == 0) {
			i12x12.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			i12x12.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			i12x12.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &i12x12.tiles);
			i12x12.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}

		if (SDL_strcmp(str, "background") == 0) {
			background.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			background.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			background.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &background.tiles);
			background.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}

		if (SDL_strcmp(str, "extra") == 0) {
			// Works exactly like decoration
			extra.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			extra.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			extra.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &extra.tiles);
			extra.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}
		if (SDL_strcmp(str, "core") == 0) {
			// Works exactly like decoration
			core.grid_size = (float)yyjson_get_num(yyjson_obj_get(layer, "__gridSize"));
			core.offset_x = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetX"));
			core.offset_y = (float)yyjson_get_num(yyjson_obj_get(layer, "__pxTotalOffsetY"));
			emp_level_parse_tile_data(level, "gridTiles", layer, &core.tiles);
			core.tiles.tilemap = SDL_strdup(yyjson_get_str(yyjson_obj_get(layer, "__tilesetRelPath")));
			continue;
		}
	}

	emp_sublevel_t* sublevel = emp_level_create_sublevel(level, &values, &tiles, &core, &extra, &background, &i8x8, &i12x12, &i16x16);
	// sublevel->id = SDL_strdup(yyjson_get_str(yyjson_obj_get(data, "iid")));
	sublevel->offset.x = (float)yyjson_get_num(yyjson_obj_get(data, "worldX"));
	sublevel->offset.y = (float)yyjson_get_num(yyjson_obj_get(data, "worldY"));
}

u32 emp_level_query(emp_level_asset_t* level, emp_entity_type type, u32 offset)
{
	for (u32 index = offset; index < level->entities.count; index++) {
		emp_level_entity_t* entity = level->entities.entries + index;
		if (entity->type == type) {
			return index + 1;
		}
	}
	return 0;
}

emp_level_entity_t* emp_level_get(emp_level_asset_t* level, u32 at)
{
	return level->entities.entries + at;
}

void emp_load_level_asset(struct emp_asset_t* asset)
{
	emp_buffer* buffer = &asset->data;
	yyjson_doc* doc = yyjson_read((const char*)buffer->data, (size_t)buffer->size, 0);

	yyjson_val* root = yyjson_doc_get_root(doc);
	yyjson_val* sublevels = yyjson_obj_get(root, "levels");

	emp_level_asset_t* level = SDL_malloc(sizeof(*level));
	SDL_memset(level, 0, sizeof(*level));

	u64 idx, size;
	yyjson_val* current;
	yyjson_arr_foreach(sublevels, idx, size, current)
	{
		emp_load_sublevel(level, (u32)idx, current);
	}

	asset->handle = (emp_level_asset_t*)level;

	yyjson_doc_free(doc);
}

void emp_unload_level_asset(struct emp_asset_t* asset)
{
	emp_level_asset_t* level_asset = (emp_level_asset_t*)asset->handle;
	for (u32 li = 0; li < level_asset->sublevels.count; li++) {
		emp_sublevel_t* sublevel = level_asset->sublevels.entries + li;
		SDL_free(sublevel->tiles.tilemap);
		SDL_free(sublevel->core.tiles.tilemap);
	}

	SDL_free(asset->handle);
	asset->handle = NULL;
}
