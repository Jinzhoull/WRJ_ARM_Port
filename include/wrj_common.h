#ifndef WRJ_COMMON_H
#define WRJ_COMMON_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WRJ_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define WRJ_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WRJ_MAX(a, b) ((a) > (b) ? (a) : (b))
#define WRJ_CLAMP(x, lo, hi) WRJ_MIN(WRJ_MAX((x), (lo)), (hi))
#define WRJ_PI 3.14159265358979323846

typedef enum {
    WRJ_OK = 0,
    WRJ_ERR_ARGUMENT = -1,
    WRJ_ERR_IO = -2,
    WRJ_ERR_MEMORY = -3,
    WRJ_ERR_CAPACITY = -4,
    WRJ_ERR_UNSUPPORTED = -5,
    WRJ_ERR_DATA = -6
} wrj_status_t;

static inline float wrj_complex_abs2(float re, float im)
{
    return re * re + im * im;
}

static inline float wrj_complex_abs(float re, float im)
{
    return sqrtf(wrj_complex_abs2(re, im));
}

static inline float wrj_clip01(float value)
{
    return WRJ_CLAMP(value, 0.0f, 1.0f);
}

static inline int wrj_isfinitef(float value)
{
    return isfinite((double)value) != 0;
}

#endif
