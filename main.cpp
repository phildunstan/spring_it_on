#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_mixer.h"
#include "SDL_ttf.h"

#pragma warning(push)
#pragma warning(disable: 4201 4701 6001)
#include "glm/glm.hpp"
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4061 6387)
#include "fmt/format.h"
#include "fmt/printf.h"
#pragma warning(pop)


using namespace std;
using namespace std::chrono;
using namespace glm;

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;

constexpr int BALL_RADIUS = 16;

float max_speed = 1.0f;
const float initial_spring_coefficient = 0.5f;
float spring_coefficient = initial_spring_coefficient;
const float initial_transverse_damping_coefficient = 0.05f; // must be in range [0, 1]
float transverse_damping_coefficient = initial_transverse_damping_coefficient;
const float initial_tangent_damping_coefficient = 0.06f;  // must be in range [0, 1]
float tangent_damping_coefficient = initial_tangent_damping_coefficient;
float ball_mass = 1.0f;

vec2 ball_position(0, 0);
vec2 ball_velocity(0, 0);
SDL_Point mouse_position = { 0, 0 };
SDL_Texture* ball_sprite = nullptr;

constexpr int TARGET_SIZE = 32;
vec2 target_position(0, 0);
SDL_Texture* target_sprite = nullptr;

SDL_Texture* gmtk_logo_sprite = nullptr;

int score = 0;
bool done = false;

mt19937_64 random_engine;
uniform_real_distribution<float> random_float01 { 0.0f, 1.0f };

TTF_Font* font18 = nullptr;
TTF_Font* font24 = nullptr;
TTF_Font* font48 = nullptr;
TTF_Font* font72 = nullptr;

Mix_Chunk* target_audio = nullptr;
Mix_Chunk* heartbeat_audio = nullptr;

const float initial_time_until_next_heartbeat = 0.8f;
const float heartbeat_time_scale = 0.85f;
float last_time_until_next_heartbeat = 0.0f;
float time_until_next_heartbeat = 0.0f;

time_point<high_resolution_clock> last_update_time;
float target_time_bonus = 1.0f;
float max_time_available = 3.0f;
float time_remaining = 0.0f;



void Quit(int rc)
{
	exit(rc);
}

SDL_Texture* LoadSprite(const char* file, SDL_Renderer& renderer)
{
	SDL_Surface* surface = IMG_Load(file);
	if (!surface)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s\n", file, IMG_GetError());
		return nullptr;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(&renderer, surface);
	if (!texture)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create texture: %s\n", SDL_GetError());
		SDL_FreeSurface(surface);
		return nullptr;
	}
	SDL_FreeSurface(surface);

	return texture;
}

void LoadAssets(SDL_Renderer& renderer)
{
	ball_sprite = LoadSprite("assets/ball.png", renderer);
	if (!ball_sprite)
	{
		Quit(2);
	}

	target_sprite = LoadSprite("assets/target.png", renderer);
	if (!target_sprite)
	{
		Quit(2);
	}

	gmtk_logo_sprite = LoadSprite("assets/gmtk_logo.png", renderer);
	if (!target_sprite)
	{
		Quit(2);
	}

	const char* fontFilename = "assets/FFF_Tusj.ttf";
	font18 = TTF_OpenFont(fontFilename, 18);
	font24 = TTF_OpenFont(fontFilename, 24);
	font48 = TTF_OpenFont(fontFilename, 48);
	font72 = TTF_OpenFont(fontFilename, 72);
	if (!font18 || !font24 || !font48 || !font72)
	{
		SDL_Log("Failed to open TTF font %s: %s\n", fontFilename, TTF_GetError());
		Quit(2);
	}

	//heartbeat_audio = Mix_LoadWAV("assets/410378__b-train__heartbeat-looper.wav");
 //   if (!heartbeat_audio)
 //   {
	//	SDL_Log("Failed to open heartbeat audio file: %s\n", Mix_GetError());
	//	Quit(2);
 //   }
    
    target_audio = Mix_LoadWAV("assets/268756__morrisjm__dingaling.mp3");
    if (!target_audio)
    {
		SDL_Log("Failed to open target audio file: %s\n", Mix_GetError());
		Quit(2);
    }
}

vec2 GenerateTargetPosition()
{
	while (true)
	{
		const float border = 0.1f;
		vec2 position(border * WINDOW_WIDTH + (1 - 2 * border) * WINDOW_WIDTH * random_float01(random_engine), border * WINDOW_HEIGHT + (1 - 2 * border) * WINDOW_HEIGHT * random_float01(random_engine));
		if (distance(position, ball_position) < 10 * BALL_RADIUS)
			continue;
		return position;
	}
}

void Reset()
{
	SDL_GetMouseState(&mouse_position.x, &mouse_position.y);
	ball_position = vec2(mouse_position.x, mouse_position.y);
	ball_velocity = vec2(0.0f, 0.0f);

	target_position = GenerateTargetPosition();

	spring_coefficient = initial_spring_coefficient;
	transverse_damping_coefficient = initial_transverse_damping_coefficient;
	tangent_damping_coefficient = initial_tangent_damping_coefficient;

	score = 0;

	time_remaining = max_time_available;
	last_update_time = high_resolution_clock::now();

	last_time_until_next_heartbeat = initial_time_until_next_heartbeat;
	time_until_next_heartbeat = 0.0f;
}


void Update(float time_delta)
{
	vec2 total_force = vec2(0, 0);

	vec2 ball_displacement((ball_position.x - mouse_position.x), (ball_position.y - mouse_position.y));
	if (length(ball_displacement) > 2.0f) // allow a two-pixel dead zone
	{
		vec2 displacement_direction = normalize(ball_displacement);
		vec2 spring_force = -spring_coefficient * (ball_mass / (time_delta * time_delta)) * ball_displacement.length() * displacement_direction;
		total_force += spring_force;

		if (length(ball_velocity) > 0)
		{
			vec2 transverse_velocity = dot(ball_velocity, displacement_direction) * displacement_direction;
			vec2 transverse_force = -transverse_damping_coefficient * (ball_mass / time_delta) * transverse_velocity;
			total_force += transverse_force;

			vec2 tangent_direction = vec2(displacement_direction.y, -displacement_direction.x);
			vec2 tangent_velocity = dot(ball_velocity, tangent_direction) * tangent_direction;
			vec2 tangent_force = -tangent_damping_coefficient * (ball_mass / time_delta) * tangent_velocity;
			total_force += tangent_force;
		}

		ball_velocity += (total_force / ball_mass) * time_delta;

		ball_position += ball_velocity * time_delta;
	}

	float target_distance = distance(ball_position, target_position);
	if (target_distance <= BALL_RADIUS + TARGET_SIZE / 2)
	{
		target_position = GenerateTargetPosition();
		++score;
		transverse_damping_coefficient *= 0.9f;
		tangent_damping_coefficient *= 0.9f;
		//time_remaining = std::clamp(time_remaining + target_time_bonus, 0.0f, max_time_available);
		time_remaining = max_time_available;
		Mix_PlayChannel(-1, target_audio, 0 );
		last_time_until_next_heartbeat = initial_time_until_next_heartbeat;
	}

	time_until_next_heartbeat -= time_delta;
	if (time_until_next_heartbeat <= 0.0f)
	{
		//Mix_PlayChannel(-1, heartbeat_audio, 0 );
		time_until_next_heartbeat = last_time_until_next_heartbeat * heartbeat_time_scale;
		last_time_until_next_heartbeat = time_until_next_heartbeat;
	}
}

void DrawScene(SDL_Renderer& renderer)
{
	SDL_Rect target_sprite_rect = { (int)(target_position.x) - TARGET_SIZE/2, (int)(target_position.y) - TARGET_SIZE/2, TARGET_SIZE, TARGET_SIZE};
	SDL_RenderCopy(&renderer, target_sprite, NULL, &target_sprite_rect);

	//SDL_SetRenderDrawColor(&renderer, 0x25, 0x5c, 0x99, 0xff);
	SDL_SetRenderDrawColor(&renderer, 0x7e, 0xa3, 0xcc, 0xff);
	SDL_RenderDrawLine(&renderer, (int)ball_position.x, (int)ball_position.y, mouse_position.x, mouse_position.y);

	SDL_Rect ball_sprite_rect = { (int)(ball_position.x) - BALL_RADIUS, (int)(ball_position.y) - BALL_RADIUS, 2 * BALL_RADIUS, 2 * BALL_RADIUS};
	SDL_RenderCopy(&renderer, ball_sprite, NULL, &ball_sprite_rect);

	SDL_Rect empty_time_bar = { WINDOW_WIDTH - 100, WINDOW_HEIGHT / 4, 60, WINDOW_HEIGHT / 2 };
	SDL_SetRenderDrawColor(&renderer, 0xb3, 0x00, 0x1b, 0x00);
	SDL_RenderFillRect(&renderer, &empty_time_bar);

	float normalized_time_remaining = time_remaining / max_time_available;
	SDL_Rect time_bar = { empty_time_bar.x, empty_time_bar.y + empty_time_bar.h - (int)(normalized_time_remaining * empty_time_bar.h), empty_time_bar.w, (int)(normalized_time_remaining * empty_time_bar.h) };
	SDL_SetRenderDrawColor(&renderer, 0x7e, 0xa3, 0xcc, 0xff);
	SDL_RenderFillRect(&renderer, &time_bar);
}

enum class TextAlignment { Left, Center, Right };
void DrawText(SDL_Renderer& renderer, TTF_Font& font, int position_x, int position_y, const char* text, TextAlignment textAlignment, SDL_Color color)
{
	SDL_Surface *surface = TTF_RenderText_Solid(&font, text, color);
	if (!surface)
	{
		SDL_Log("Unable to draw text: %s", TTF_GetError());
		return;
	}

	SDL_Texture* texture = SDL_CreateTextureFromSurface(&renderer, surface);
	if (!texture)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create texture: %s\n", SDL_GetError());
		SDL_FreeSurface(surface);
		return;
	}

	int left_position = position_x;
	if (textAlignment == TextAlignment::Center)
		left_position = position_x - surface->w / 2;
	else if (textAlignment == TextAlignment::Right)
		left_position = position_x - surface->w;

	SDL_Rect rect = { left_position, position_y, surface->w, surface->h };
	SDL_RenderCopy(&renderer, texture, NULL, &rect);
	SDL_DestroyTexture(texture);
}

void DrawFrontEnd(SDL_Renderer& renderer)
{
	const SDL_Color color = {0x25, 0x5c, 0x99, 0xff};
	DrawText(renderer, *font72, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 150, "Spring It On", TextAlignment::Center, color);
	DrawText(renderer, *font48, WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 50, "Press SPACE to start", TextAlignment::Center, color);

	DrawText(renderer, *font24, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 104, "Written by Philip Dunstan", TextAlignment::Center, color);
	DrawText(renderer, *font24, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 72, "Tested by Katherine Thomson", TextAlignment::Center, color);
	DrawText(renderer, *font24, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 40, "Source available at https://github.com/phildunstan/spring_it_on", TextAlignment::Center, color);
}

void DrawBackgroundUI(SDL_Renderer& renderer)
{
	SDL_Rect gmtk_logo_sprite_rect = { 0, WINDOW_HEIGHT - 100, 200, 100};
	SDL_RenderCopy(&renderer, gmtk_logo_sprite, NULL, &gmtk_logo_sprite_rect);
}

void DrawUI(SDL_Renderer& renderer)
{
	const SDL_Color color = {0x25, 0x5c, 0x99, 0xff};
	DrawText(renderer, *font48, WINDOW_WIDTH - 280, 10, fmt::format("Score:", score).c_str(), TextAlignment::Left, color);
	DrawText(renderer, *font48, WINDOW_WIDTH - 30, 10, fmt::format("{}", score).c_str(), TextAlignment::Right, color);
}

void DrawDebugUI(SDL_Renderer& renderer)
{
	const SDL_Color color = { 0, 0, 0, 0xff };
	DrawText(renderer, *font18, 10, 10, fmt::format("spring_coefficient -> {}", spring_coefficient).c_str(), TextAlignment::Left, color);
	DrawText(renderer, *font18, 10, 30, fmt::format("transverse_damping_coefficient -> {}", transverse_damping_coefficient).c_str(), TextAlignment::Left, color);
	DrawText(renderer, *font18, 10, 50, fmt::format("tangent_damping_coefficient -> {}", tangent_damping_coefficient).c_str(), TextAlignment::Left, color);
}
 	
void ProcessEvents()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_QUIT:
			done = true;
			break;
		case SDL_KEYDOWN:
		{
			switch (event.key.keysym.sym)
			{
			case SDLK_ESCAPE:
				done = true;
				break;
			case SDLK_SPACE:
				Reset();
				break;
			case SDLK_1:
				spring_coefficient *= 1.5f;
				fmt::print("spring_coefficient -> {}\n", spring_coefficient);
				break;
			case SDLK_2:
				spring_coefficient *= 0.66f;
				fmt::print("spring_coefficient -> {}\n", spring_coefficient);
				break;
			case SDLK_3:
				transverse_damping_coefficient *= 1.5f;
				fmt::print("transverse_damping_coefficient -> {}\n", transverse_damping_coefficient);
				break;
			case SDLK_4:
				transverse_damping_coefficient *= 0.66f;
				fmt::print("transverse_damping_coefficient -> {}\n", transverse_damping_coefficient);
				break;
			case SDLK_5:
				tangent_damping_coefficient *= 1.5f;
				fmt::print("tangent_damping_coefficient -> {}\n", tangent_damping_coefficient);
				break;
			case SDLK_6:
				tangent_damping_coefficient *= 0.66f;
				fmt::print("tangent_damping_coefficient -> {}\n", tangent_damping_coefficient);
				break;
			}

			break;
		}
		case SDL_MOUSEMOTION:
		{
			if (time_remaining > 0)
			{
				mouse_position.x = event.motion.x;
				mouse_position.y = event.motion.y;
			}
			break;
		}
		}
	}
}

int main(int /*argc*/, char** /*argv*/)
{
	SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

	random_engine.seed(high_resolution_clock::now().time_since_epoch().count());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_AUDIO) != 0)
	{
        SDL_Log("Failed to init SDL: %s", SDL_GetError());
		Quit(1);
    }

	constexpr int IMG_INIT_FLAGS = IMG_INIT_PNG;
	if (IMG_Init(IMG_INIT_FLAGS) != (IMG_INIT_FLAGS))
	{
        SDL_Log("Failed to init required PNG support: %s", IMG_GetError());
		Quit(1);
	}
 	
	if (TTF_Init() != 0)
	{
        SDL_Log("Failed to init TTF: %s", TTF_GetError());
		Quit(1);
	}
 	
	constexpr int MIX_INIT_FLAGS = MIX_INIT_MP3;
	if (Mix_Init(MIX_INIT_FLAGS) != MIX_INIT_FLAGS)
	{
		SDL_Log("Failed to init required MP3 support: %s", Mix_GetError());
		Quit(1);
	}
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
	{
		SDL_Log("Failed to init SDL_mixer: %s", Mix_GetError());
		Quit(1);
	}

	SDL_Window* window = SDL_CreateWindow("Spring It On", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
	if (!window)
	{
        SDL_Log("Unable to create SDL window: %s", SDL_GetError());
		Quit(1);
	}
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer)
	{
        SDL_Log("Unable to create SDL renderer: %s", SDL_GetError());
		Quit(1);
	}

	LoadAssets(*renderer);

	Reset();
	time_remaining = 0;

	while (!done)
	{
		SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
		SDL_RenderClear(renderer);

		ProcessEvents();

		if (time_remaining > 0.0f)
		{
			const time_point<high_resolution_clock> current_time = high_resolution_clock::now();
			const float time_delta = std::clamp(duration_cast<microseconds>(current_time - last_update_time).count() / 1e6f, 0.0f, 0.1f);
			if (time_delta > 0.001f)
				Update(time_delta);
			time_remaining = std::max(time_remaining - time_delta, 0.0f);
			last_update_time = current_time;
		}

		//SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
		SDL_SetRenderDrawColor(renderer, 0xcc, 0xad, 0x8f, 0xff);
		SDL_RenderClear(renderer);

		DrawBackgroundUI(*renderer);
		DrawScene(*renderer);
		DrawUI(*renderer);
		//DrawDebugUI(*renderer);

		if (time_remaining <= 0.0f)
			DrawFrontEnd(*renderer);

		SDL_RenderPresent(renderer);
	}

	TTF_Quit();
	IMG_Quit();
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}

