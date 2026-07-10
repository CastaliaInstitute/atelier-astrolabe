#pragma once

#include <stddef.h>

#define TINFL_FLAG_PARSE_ZLIB_HEADER 1

static inline size_t tinfl_decompress_mem_to_mem(void *out_buf,
                                                 size_t out_buf_len,
                                                 const void *src_buf,
                                                 size_t src_buf_len,
                                                 int flags)
{
    (void)out_buf;
    (void)out_buf_len;
    (void)src_buf;
    (void)src_buf_len;
    (void)flags;
    return 0;
}
