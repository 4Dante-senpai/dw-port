#include "host.h"
#include "../plat.h"
#include <stdatomic.h>
extern void port_pad_latch(uint16_t wire);
static _Atomic uint16_t buttons;
static SDL_Gamepad *pad;
static const struct { SDL_Scancode key; unsigned bit; } keys[] = {
    {SDL_SCANCODE_BACKSPACE, 0}, {SDL_SCANCODE_RETURN, 3},
    {SDL_SCANCODE_UP, 4}, {SDL_SCANCODE_W, 4}, {SDL_SCANCODE_RIGHT, 5}, {SDL_SCANCODE_D, 5},
    {SDL_SCANCODE_DOWN, 6}, {SDL_SCANCODE_S, 6}, {SDL_SCANCODE_LEFT, 7}, {SDL_SCANCODE_A, 7},
    {SDL_SCANCODE_1, 8}, {SDL_SCANCODE_3, 9}, {SDL_SCANCODE_Q, 10}, {SDL_SCANCODE_E, 11},
    {SDL_SCANCODE_I, 12}, {SDL_SCANCODE_L, 13}, {SDL_SCANCODE_K, 14}, {SDL_SCANCODE_J, 15}
};
static const struct { SDL_GamepadButton button; unsigned bit; } gamepad[] = {
    {SDL_GAMEPAD_BUTTON_BACK, 0}, {SDL_GAMEPAD_BUTTON_START, 3},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 4}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 5},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 6}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 7},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 10}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 11},
    {SDL_GAMEPAD_BUTTON_NORTH, 12}, {SDL_GAMEPAD_BUTTON_EAST, 13},
    {SDL_GAMEPAD_BUTTON_SOUTH, 14}, {SDL_GAMEPAD_BUTTON_WEST, 15}
};
static void open_pad(void)
{
    int count;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; !pad && i < count; i++) pad = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
}
void sdl_input_event(const SDL_Event *event)
{
    if (event->type == SDL_EVENT_GAMEPAD_REMOVED && pad &&
        event->gdevice.which == SDL_GetGamepadID(pad)) { SDL_CloseGamepad(pad); pad = NULL; }
    if (event->type == SDL_EVENT_GAMEPAD_ADDED || event->type == SDL_EVENT_GAMEPAD_REMOVED) open_pad();
}
uint16_t sdl_input_mask(const bool *keyboard, bool focused, const bool *pad_buttons,
                        int16_t left_trigger, int16_t right_trigger)
{
    uint16_t mask = 0;
    if (focused)
        for (unsigned i = 0; i < sizeof(keys)/sizeof(*keys); i++)
            if (keyboard[keys[i].key]) mask |= 1u << keys[i].bit;
    for (unsigned i = 0; i < sizeof(gamepad)/sizeof(*gamepad); i++)
        if (pad_buttons[gamepad[i].button]) mask |= 1u << gamepad[i].bit;
    if (left_trigger > 16384) mask |= 1u << 8;
    if (right_trigger > 16384) mask |= 1u << 9;
    return mask;
}
void sdl_input_poll(void)
{
    if (sdl_headless) return;
    if (!pad) open_pad();
    bool pad_buttons[SDL_GAMEPAD_BUTTON_COUNT] = {0};
    int16_t left = 0, right = 0;
    if (pad) {
        for (unsigned i = 0; i < sizeof(gamepad)/sizeof(*gamepad); i++)
            pad_buttons[gamepad[i].button] = SDL_GetGamepadButton(pad, gamepad[i].button);
        left = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        right = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    }
    uint16_t mask = sdl_input_mask(SDL_GetKeyboardState(NULL), SDL_GetKeyboardFocus() != NULL,
                                   pad_buttons, left, right);
    atomic_store_explicit(&buttons, mask, memory_order_relaxed);
}
void plat_input_vblank(void)
{
    if (!sdl_headless) port_pad_latch(atomic_load_explicit(&buttons, memory_order_relaxed));
}
void sdl_input_quit(void) { SDL_CloseGamepad(pad); pad = NULL; }
