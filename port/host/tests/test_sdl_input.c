#include "../sdl/host.h"
#include "../plat.h"
#include <assert.h>
#include <stdio.h>
int sdl_headless;
static uint16_t latched;
void port_pad_latch(uint16_t wire) { latched = wire; }
static void pump(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) sdl_input_event(&event);
    sdl_input_poll(); plat_input_vblank();
}
static void keyboard_masks(void)
{
    const struct { SDL_Scancode key; unsigned bit; } aliases[] = {
        {SDL_SCANCODE_BACKSPACE, 0}, {SDL_SCANCODE_RETURN, 3},
        {SDL_SCANCODE_UP, 4}, {SDL_SCANCODE_W, 4}, {SDL_SCANCODE_RIGHT, 5}, {SDL_SCANCODE_D, 5},
        {SDL_SCANCODE_DOWN, 6}, {SDL_SCANCODE_S, 6}, {SDL_SCANCODE_LEFT, 7}, {SDL_SCANCODE_A, 7},
        {SDL_SCANCODE_1, 8}, {SDL_SCANCODE_3, 9}, {SDL_SCANCODE_Q, 10}, {SDL_SCANCODE_E, 11},
        {SDL_SCANCODE_I, 12}, {SDL_SCANCODE_L, 13}, {SDL_SCANCODE_K, 14}, {SDL_SCANCODE_J, 15}
    };
    bool keyboard[SDL_SCANCODE_COUNT] = {0}, buttons[SDL_GAMEPAD_BUTTON_COUNT] = {0};
    for (unsigned i = 0; i < sizeof(aliases)/sizeof(*aliases); i++) {
        keyboard[aliases[i].key] = true;
        uint16_t bit = 1u << aliases[i].bit;
        assert(sdl_input_mask(keyboard, true, buttons, 0, 0) == bit);
        assert(sdl_input_mask(keyboard, false, buttons, 0, 0) == 0);
        buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true;
        assert(sdl_input_mask(keyboard, true, buttons, 32767, 32767) ==
               (bit | (1u << 14) | (1u << 8) | (1u << 9)));
        assert(sdl_input_mask(keyboard, false, buttons, 32767, 32767) ==
               ((1u << 14) | (1u << 8) | (1u << 9)));
        buttons[SDL_GAMEPAD_BUTTON_SOUTH] = false;
        keyboard[aliases[i].key] = false;
    }
    keyboard[SDL_SCANCODE_UP] = keyboard[SDL_SCANCODE_W] = true;
    assert(sdl_input_mask(keyboard, true, buttons, 16384, 16384) == (1u << 4));
    for (unsigned i = 0; i < SDL_SCANCODE_COUNT; i++) keyboard[i] = true;
    assert(sdl_input_mask(keyboard, false, buttons, 0, 0) == 0);
    assert(sdl_input_mask(keyboard, true, buttons, 0, 0) == 0xfff9);
}
int main(void)
{
    keyboard_masks();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.name = "DW1 test gamepad";
    const unsigned bits[] = {14, 13, 15, 12, 0, 0, 3, 0, 0, 10, 11, 4, 6, 7, 5};
    for (unsigned connection = 0; connection < 2; connection++) {
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        assert(id && SDL_IsGamepad(id));
        SDL_Joystick *joystick = SDL_OpenJoystick(id);
        assert(joystick);
        pump(); assert(latched == 0);
        for (unsigned button = 0; button < sizeof(bits)/sizeof(*bits); button++) {
            if (button == SDL_GAMEPAD_BUTTON_GUIDE || button == SDL_GAMEPAD_BUTTON_LEFT_STICK ||
                button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) continue;
            assert(SDL_SetJoystickVirtualButton(joystick, button, true));
            pump(); assert(latched == (1u << bits[button]));
            assert(SDL_SetJoystickVirtualButton(joystick, button, false));
            pump(); assert(latched == 0);
        }
        assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767));
        assert(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767));
        pump(); assert(latched == ((1u << 8) | (1u << 9)));
        SDL_CloseJoystick(joystick);
        assert(SDL_DetachVirtualJoystick(id));
        pump(); assert(latched == 0);
    }
    sdl_input_quit(); SDL_Quit();
    puts("sdl_input: positional buttons, triggers, hotplug and release OK");
    return 0;
}
