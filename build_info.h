#pragma once
/** Čas kompilácie (GCC). SHA / repo generuje tools/gen_build_info.py → build_info_generated.h. */
#include "build_info_generated.h"
#define FIRMWARE_BUILD_DATETIME (__DATE__ " " __TIME__)
