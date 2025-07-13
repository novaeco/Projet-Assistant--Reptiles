#include "unity.h"

extern void test_display_dummy(void);
extern void test_keyboard_initial_state(void);

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_display_dummy);
    RUN_TEST(test_keyboard_initial_state);
    UNITY_END();
}
