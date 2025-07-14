#include "mock_keyboard.h"
uint16_t mock_keyboard_state = 0;
uint16_t keyboard_get_state(void) { return mock_keyboard_state; }
