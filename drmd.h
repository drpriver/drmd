#ifndef DRMD_H
#define DRMD_H
#include "stringview.h"
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#ifndef DRMD_API
#define DRMD_API extern
#endif

DRMD_API
int drmd_to_html(StringView input, StringView* output);

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
