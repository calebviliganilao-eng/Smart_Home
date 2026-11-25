#pragma once
#include <stdbool.h>

#define RELAY_ACTIVE_LOW 0

static inline int relay_output_level(bool on)
{
    return RELAY_ACTIVE_LOW ? !on : on;
}
