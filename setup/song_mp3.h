// minimp3 (https://github.com/lieff/minimp3, CC0 / public domain), with its own warnings left out of the build's -Wall -Wextra -Werror.
// Include from ONE .cpp per program: it brings in the decoder itself.
#pragma once
#pragma GCC system_header
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"
