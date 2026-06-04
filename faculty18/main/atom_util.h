#pragma once

#include <stdio.h>
#include <stddef.h>

static inline void atom_strlcpy(char *dst, const char *src, size_t cap)
{
    if (cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}
