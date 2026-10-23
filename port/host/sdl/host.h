#ifndef PORT_SDL_HOST_H
#define PORT_SDL_HOST_H
#include <SDL3/SDL.h>
extern int sdl_headless;
int sdl_video_init(void);
void sdl_video_draw(void);
void sdl_video_quit(void);
void sdl_input_event(const SDL_Event *event);
uint16_t sdl_input_mask(const bool *keyboard, bool focused, const bool *pad_buttons,
                        int16_t left_trigger, int16_t right_trigger);
void sdl_input_poll(void);
void sdl_input_quit(void);
void sdl_audio_init(void);
void sdl_audio_quit(void);
#ifdef DW1_SDL_TEST
SDL_Renderer *sdl_video_test_renderer(void);
unsigned sdl_audio_test_level(void);
#endif
#endif
