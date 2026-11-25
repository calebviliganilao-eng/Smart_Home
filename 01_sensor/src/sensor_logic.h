#pragma once
#include <stdbool.h>

// Set to 1 if the module outputs LOW when covered.
#define DARK_WHEN_DO_LOW 0

static inline bool sensor_is_dark(int do_level)
{
    return DARK_WHEN_DO_LOW ? do_level == 0 : do_level != 0;
}

static inline unsigned char sensor_onoff_from_do(int do_level)
{
    return sensor_is_dark(do_level) ? 1 : 0;
}
