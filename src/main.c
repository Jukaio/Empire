#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <Empire/text.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <stddef.h>

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#include "entities.h"

#include <Empire/generated/assets_generated.h>
#include <Empire/level.h>
#include <Empire/miniaudio.h>
#include <Empire/stb_image.h>
#include <Empire/types.h>
#include <Empire/util.h>

typedef struct
{
	float m[16];
} emp_mat4_t;

static SDL_Window* g_window = NULL;
static SDL_Renderer* g_renderer = NULL;
static emp_generated_assets_o* g_assets = NULL;
static emp_asset_manager_o* g_asset_mgr = NULL;
static Uint64 g_last_time = 0;
static bool g_running = true;
static ma_engine g_audio_engine;

void update_sprite_magnification(void)
{
	int width, height;
	SDL_GetWindowSize(g_window, &width, &height);

	float raw_mag = 4.0f * (float)height / 1080.0f;

	int snapped = (int)roundf(raw_mag);
	if (snapped < 2)
		snapped = 2;
	if (snapped > 6)
		snapped = 6;

	SPRITE_MAGNIFICATION = (float)snapped;
}

void main_loop(void)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_QUIT) {
			g_running = false;
		}
		if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
			g_running = false;
		}
		if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			update_sprite_magnification();
		}
	}

	Uint64 current_time = SDL_GetTicks();
	double delta_time = (current_time - g_last_time) / 1000.0;
	delta_time = SDL_min(delta_time, 0.5f);
	g_last_time = current_time;
	G->args->dt = (float)delta_time;
	G->args->global_time += delta_time;

	SDL_SetRenderDrawColor(g_renderer, 17, 25, 45, 1);
	SDL_RenderClear(g_renderer);

	emp_entities_update();

	int win_w, win_h;
	SDL_GetWindowSize(g_window, &win_w, &win_h);

	char buffer2[64];
	SDL_snprintf(buffer2, sizeof(buffer2), "");
	emp_draw_text((float)win_w / 2 - 200, 100, buffer2, 187, 195, 208, &g_assets->ttf->asepritefont);

	SDL_RenderPresent(g_renderer);
}

int parse_atlas_width_from_path_name(const char* path)
{
	const char* dot = SDL_strrchr(path, '.');
	const char* underscore = SDL_strrchr(path, '_');
	if (underscore && (!dot || underscore < dot)) {
		return SDL_atoi(underscore + 1);
	}
	return 0;
}

void emp_png_load_func(emp_asset_t* asset)
{
	int width, height, channels;
	unsigned char* data = stbi_load_from_memory(asset->data.data, (int)asset->data.size, &width, &height, &channels, 4);

	SDL_Surface* surface = SDL_CreateSurfaceFrom(
		width, height, SDL_PIXELFORMAT_RGBA32, data, width * 4);

	SDL_Texture* texture = SDL_CreateTextureFromSurface(g_renderer, surface);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	SDL_DestroySurface(surface);
	stbi_image_free(data);
	emp_texture_t* emp_tex = SDL_malloc(sizeof(emp_texture_t));

	emp_tex->texture = texture;

	int atlas_size = parse_atlas_width_from_path_name(asset->path);
	if (atlas_size) {
		emp_tex->source_size = (u32)(atlas_size);
		emp_tex->columns = width / atlas_size;
		emp_tex->rows = height / atlas_size;
		emp_tex->width = (float)atlas_size;
		emp_tex->height = (float)atlas_size;
	} else {
		emp_tex->source_size = width;
		emp_tex->rows = 1;
		emp_tex->columns = 1;
		emp_tex->width = (float)width;
		emp_tex->height = (float)height;
	}

	asset->handle = emp_tex;
}

void emp_png_unload_func(emp_asset_t* asset)
{
	emp_texture_t* emp_tex = asset->handle;
	SDL_DestroyTexture(emp_tex->texture);
	SDL_free(emp_tex);
}

const char* get_asset_argument(int argc, char* arguments[])
{
	for (int index = 0; index < argc; index++) {
		char* arg = arguments[index];
		const char token[] = "cwd=";
		char* path = SDL_strstr(arg, token);

		if (path != NULL) {
			return path + sizeof(token) - 1; // Null terminator
		}
	}
	return "";
}

typedef struct emp_audio_t
{
	ma_decoder decoder;
	const void* data;
	size_t size;
} emp_audio_t;

void emp_load_ogg_asset(struct emp_asset_t* asset)
{
	emp_audio_t* audio = SDL_malloc(sizeof(emp_audio_t));
	audio->data = asset->data.data;
	audio->size = asset->data.size;

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, g_audio_engine.sampleRate);
	ma_result result = ma_decoder_init_memory(audio->data, audio->size, &config, &audio->decoder);
	if (result != MA_SUCCESS) {
		SDL_Log("Failed to load OGG asset '%s': miniaudio error %d", asset->path, result);
		SDL_free(audio);
		asset->handle = NULL;
		return;
	}
	asset->handle = audio;
}
void emp_unload_ogg_asset(struct emp_asset_t* asset)
{
	emp_audio_t* audio = asset->handle;
	if (audio) {
		ma_decoder_uninit(&audio->decoder);
		SDL_free(audio);
	}
}

#ifdef __EMSCRIPTEN__
EM_BOOL on_canv_resize(int eventType, const EmscriptenUiEvent* uiEvent, void* userData)
{

	SDL_SetWindowSize(g_window, (int)uiEvent->windowInnerWidth, (int)uiEvent->windowInnerHeight);

	emscripten_set_element_css_size("#canvas", uiEvent->windowInnerWidth, uiEvent->windowInnerHeight);

	SDL_Log("Resized to: %d x %d", (int)uiEvent->windowInnerWidth, (int)uiEvent->windowInnerHeight);
	return EM_TRUE;
}
#endif

emp_compressed_buffer write_game_snapshot()
{
	u64 args_size = sizeof(emp_update_args_t);
	u64 player_size = sizeof(emp_player_t) * EMP_MAX_PLAYERS;
	u64 enemy_size = sizeof(emp_enemy_t) * EMP_MAX_ENEMIES;
	u64 spawner_size = sizeof(emp_spawner_t) * EMP_MAX_SPAWNERS;
	u64 bullet_size = sizeof(emp_bullet_t) * EMP_MAX_BULLETS;
	u64 tile_health_size = sizeof(emp_tile_health_t) * EMP_LEVEL_TILES;

	u64 total_size = 0;

	total_size += args_size;
	total_size += player_size;
	total_size += enemy_size;
	total_size += spawner_size;
	total_size += bullet_size;
	total_size += tile_health_size;

	static emp_buffer scratch_buffer;
	if (scratch_buffer.size != total_size) {
		scratch_buffer.data = SDL_realloc(scratch_buffer.data, total_size);
		scratch_buffer.size = total_size;
	}

	u64 write_pos = 0;

	SDL_memcpy(scratch_buffer.data + write_pos, G->args, args_size);
	write_pos += args_size;

	SDL_memcpy(scratch_buffer.data + write_pos, G->player, player_size);
	write_pos += player_size;

	SDL_memcpy(scratch_buffer.data + write_pos, G->enemies, enemy_size);
	write_pos += enemy_size;

	SDL_memcpy(scratch_buffer.data + write_pos, G->spawners, spawner_size);
	write_pos += spawner_size;

	SDL_memcpy(scratch_buffer.data + write_pos, G->bullets, bullet_size);
	write_pos += bullet_size;

	SDL_memcpy(scratch_buffer.data + write_pos, G->level->health, tile_health_size);

	return emp_compress_buffer(scratch_buffer);
}

void restore_game_snapshot(emp_compressed_buffer compressed_buffer)
{
	emp_buffer state_buffer = emp_decompress_buffer(compressed_buffer);

	u64 args_size = sizeof(emp_update_args_t);
	u64 player_size = sizeof(emp_player_t) * EMP_MAX_PLAYERS;
	u64 enemy_size = sizeof(emp_enemy_t) * EMP_MAX_ENEMIES;
	u64 spawner_size = sizeof(emp_spawner_t) * EMP_MAX_SPAWNERS;
	u64 bullet_size = sizeof(emp_bullet_t) * EMP_MAX_BULLETS;
	u64 tile_health_size = sizeof(emp_tile_health_t) * EMP_LEVEL_TILES;

	u64 read_pos = 0;

	SDL_memcpy(G->args, state_buffer.data + read_pos, args_size);
	read_pos += args_size;

	SDL_memcpy(G->player, state_buffer.data + read_pos, player_size);
	read_pos += player_size;

	SDL_memcpy(G->enemies, state_buffer.data + read_pos, enemy_size);
	read_pos += enemy_size;

	SDL_memcpy(G->spawners, state_buffer.data + read_pos, spawner_size);
	read_pos += spawner_size;

	SDL_memcpy(G->bullets, state_buffer.data + read_pos, bullet_size);
	read_pos += bullet_size;

	SDL_memcpy(G->level->health, state_buffer.data + read_pos, tile_health_size);
	read_pos += tile_health_size;

	emp_free_buffer(&state_buffer);
}

int main(int argc, char* argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		return 1;
	}
	// TODO: Presentation mode
	int win_w = 1920;
	int win_h = 1152;

	// SDL_DisplayID display = SDL_GetPrimaryDisplay();
	// const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
	// int win_w = (int)(mode->w * 0.8f);
	// int win_h = (int)(mode->h * 0.8f);
	SDL_CreateWindowAndRenderer("Empire", win_w, win_h, SDL_WINDOW_RESIZABLE, &g_window, &g_renderer);
	SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

	SDL_SetRenderVSync(g_renderer, 1);

	if (!g_window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	const char* root = get_asset_argument(argc, argv);

	g_assets = emp_generated_assets_create(root);
	g_asset_mgr = emp_asset_manager_create(g_assets);

	emp_asset_loader_t png_loader = {
		.load = &emp_png_load_func,
		.unload = &emp_png_unload_func,
	};

	emp_asset_loader_t ldtk_loader = {
		.load = &emp_load_level_asset,
		.unload = &emp_unload_level_asset,
	};

	emp_asset_loader_t ogg_loader = {
		.load = &emp_load_ogg_asset,
		.unload = &emp_unload_ogg_asset,
	};

	G = SDL_malloc(sizeof(emp_G));

	ma_result result = ma_engine_init(NULL, &g_audio_engine);
	if (result != MA_SUCCESS) {
		SDL_Log("Failed to initialize miniaudio engine: %d", result);
	}
	G->mixer = &g_audio_engine;

	emp_load_font(g_renderer, &g_assets->ttf->bauhs93, 84.0f);
	emp_load_font(g_renderer, &g_assets->ttf->asepritefont, 84.0f);
	emp_asset_manager_add_loader(g_asset_mgr, png_loader, EMP_ASSET_TYPE_PNG);
	emp_asset_manager_add_loader(g_asset_mgr, ldtk_loader, EMP_ASSET_TYPE_LDTK);
	emp_asset_manager_add_loader(g_asset_mgr, ogg_loader, EMP_ASSET_TYPE_OGG);

	emp_asset_manager_check_hot_reload(g_asset_mgr, 10.0f);

	G->assets = g_assets;
	G->args = SDL_malloc(sizeof(emp_update_args_t));
	G->renderer = g_renderer;

	emp_entities_init();
	emp_init_enemy_configs();
	emp_init_weapon_configs();

	emp_create_level(&G->assets->ldtk->presentation, 0);

	emp_music_player_init();

	update_sprite_magnification();

	SDL_zerop(G->args);
	g_last_time = SDL_GetTicks();

#ifdef __EMSCRIPTEN__
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, on_canv_resize);
	emscripten_set_main_loop(main_loop, 0, 1);
#else
	u64 last_time = SDL_GetTicks() - 900;
	u64 frame_count = 0;

	u32 prev_state_write = 0;
	emp_compressed_buffer* prvious_states = SDL_calloc(1024, sizeof(emp_compressed_buffer));

	while (g_running) {
		frame_count++;
		u64 currentTime = SDL_GetTicks();
		if (currentTime - last_time >= 100) {
			char title[64];
			SDL_snprintf(title, sizeof(title), "My App - FPS: %llu", frame_count);
			SDL_SetWindowTitle(g_window, title);
			frame_count = 0;
			last_time = currentTime;

			emp_compressed_buffer* next = &prvious_states[prev_state_write];
			if (next->data) {
				SDL_free(next->data);
				next->data = NULL;
			}
			*next = write_game_snapshot();
			prev_state_write = prev_state_write + 1 & (1024 - 1);
		}

		const bool* keys = SDL_GetKeyboardState(NULL);
		if (keys[SDL_SCANCODE_R]) {
			u32 slot = (prev_state_write - 1) & (1024 - 1);
			if (prvious_states[slot].original_size > 0) {
				restore_game_snapshot(prvious_states[slot]);
				prev_state_write = slot;
			}
		}

		main_loop();
		emp_asset_manager_check_hot_reload(g_asset_mgr, G->args->dt);
	}
#endif
	SDL_DestroyWindow(g_window);
	SDL_Quit();

	return 0;
}
