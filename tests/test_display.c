#include "unity.h"
#include "display.h"

void test_display_dummy(void)
{
    /* Simply ensure the display header can be included and functions link */
    TEST_ASSERT_NOT_NULL(display_init);
    TEST_ASSERT_NOT_NULL(display_update);
}
