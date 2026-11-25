#pragma once
#include <stdio.h>

#define UNITY_OUTPUT_CHAR(c) putchar(c)
#define UNITY_OUTPUT_FLUSH() ((void)fflush(stdout))
