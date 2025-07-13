#pragma once

/** Initialize matrix keyboard scanning */
void keyboard_init(void);

/** Retrieve latest key state as bitmask */
uint16_t keyboard_get_state(void);

