//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#include "drmd.c"

int 
LLVMFuzzerTestOneInput(const unsigned char* data, size_t size){
    StringView input = {size, (const char*)data};
    StringView output = {0};
    int e = drmd_to_html(input, &output);
    if(!e) free((char*)output.text);
    return 0;
}
