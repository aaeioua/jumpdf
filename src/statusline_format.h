#pragma once

#include "statusline_layout.h"

void statusline_format(const StatuslineLayout *layout,
    const StatuslineState *state, gchar *out[STATUSLINE_N_SECTIONS]);
