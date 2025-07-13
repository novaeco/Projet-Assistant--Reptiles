#include "unity.h"
#include "keyboard.h"

void test_keyboard_initial_state(void)
{
    /* Without initialization keyboard state should be zero */
    TEST_ASSERT_EQUAL_UINT16(0, keyboard_get_state());
}
