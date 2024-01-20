//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#ifndef JSINTER_H
#define JSINTER_H
#include <stdlib.h>
#include <string.h>
#include "../stringview.h"
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif
// Javascript will malloc on our heap and fill out this structure.
typedef struct PString PString;
struct PString {
    // length does not include nul
    size_t length;
    // nul terminated
    unsigned char text[];
};

static inline
StringView
PString_to_sv(PString* pstr){
    StringView text = {.text=(char*)pstr->text, .length=pstr->length};
    return text;
}

static inline
PString*
StringView_to_new_PString(StringView source){
    PString* result = malloc(sizeof(*result)+source.length+1);
    result->length = source.length;
    memcpy(result->text, source.text, source.length);
    result->text[result->length] = 0;
    return result;
}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
