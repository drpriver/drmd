//
// Copyright © 2023-2024, David Priver <david@davidpriver.com>
//
#ifndef STRINGVIEW_H
#define STRINGVIEW_H
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

typedef struct StringView StringView;
struct StringView {
    size_t length;
    const char* text;
};

#define SV(x) ((StringView){sizeof(x)-1, x})
#define SVI(x) {sizeof(x)-1, x}

force_inline
_Bool
sv_equals(StringView a, StringView b){
    if(a.length != b.length) return 0;
    return memcmp(a.text, b.text, a.length) == 0;
}

force_inline
_Bool
sv_equals2(StringView a, const char* txt, size_t len){
    if(a.length != len) return 0;
    return memcmp(a.text, txt, len) == 0;
}

static inline
_Bool 
sv_iequals(StringView a, StringView b){
    if(a.length != b.length) return 0;
    size_t length = a.length;
    const uint8_t* ap = (const uint8_t*)a.text;
    const uint8_t* bp = (const uint8_t*)b.text;
    for(size_t i = 0; i < length; i++){
        uint8_t l = ap[i];
        l |= 0x20u;
        uint8_t r = bp[i];
        r |= 0x20u;
        if(l != r) return 0;
    }
    return 1;
}

static inline
_Bool
sv_iequals2(StringView a, const char* txt, size_t len){
    return sv_iequals(a, (StringView){len, txt});
}

force_inline
void
lstrip(StringView* sv){
    while(sv->length){
        switch(sv->text[0]){
            case ' ':
            // case '\n':
            // case '\t':
                sv->length--;
                sv->text++;
                continue;
            default:
                return;
        }
    }
}

force_inline
void
rstrip(StringView* sv){
    while(sv->length){
        switch(sv->text[sv->length-1]){
            case ' ':
            // case '\n':
            // case '\t':
                sv->length--;
                continue;
            default:
                return;
        }
    }
}

force_inline
void
lstripc(StringView* sv){
    while(sv->length){
        switch(sv->text[0]){
            case ' ':
            // case '\n':
            // case '\t':
            case ',':
                sv->length--;
                sv->text++;
                continue;
            default:
                return;
        }
    }
}

force_inline
StringView
stripped(StringView sv){
    lstrip(&sv);
    rstrip(&sv);
    return sv;
}

force_inline
StringView
stripped2(const char* txt, size_t len){
    StringView sv = {len, txt};
    lstrip(&sv);
    rstrip(&sv);
    return sv;
}


#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
