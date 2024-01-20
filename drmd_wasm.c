//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#define DRMD_API static
#include "drmd.h"
#include "Wasm/jsinter.h"

extern
PString*
make_html(PString* source){
    StringView text = PString_to_sv(source);
    StringView output;
    int e = drmd_to_html(text, &output);
    if(e) return NULL;
    PString* result = StringView_to_new_PString(output);
    return result;
}

#include "drmd.c"
