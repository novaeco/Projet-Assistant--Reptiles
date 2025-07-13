#pragma once

/** Initialize optional touch controller */
void touch_init(void);

/** Read touch coordinates, returns true if touched */
bool touch_read(uint16_t *x, uint16_t *y);

