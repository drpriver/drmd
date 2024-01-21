//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#include <stdio.h>
#include <errno.h>
#include "stringview.h"
#define PARSE_NUMBER_PARSE_FLOATS 0
#include "argument_parsing.h"
#include "MStringBuilder.h"
#include "Allocators/mallocator.h"
#include "term_util.h"

#define DRMD_API static
#include "drmd.h"

// One day there will be #embed...
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__IMPORTC__)
#define EMBEDDED_STYLESHEET 1
extern const char _readme_stylesheet[] asm("__readme_stylesheet");
asm(
    ".global __readme_stylesheet\n"
    ".align 8\n"
    "__readme_stylesheet:\n"
#ifndef README_CSS_PATH
#define README_CSS_PATH "README.css"
#endif
    ".incbin \"" README_CSS_PATH "\"\n"
    // Guarantee a nul at the end.
    ".byte 0\n"
    ".align 4\n"
);
#else
#endif


int 
main(int argc, const char** argv){
    StringView src = {0};
    StringView dst = {0};
    StringView stylesheet = {0};
    _Bool no_stylesheet = 0;
    ArgToParse pos_args[] = {
        {
            .name = SV("src"),
            .dest = ARGDEST(&src),
            .min_num = 0, .max_num = 1,
            .help = "md file",
        },
    };
    ArgToParse kw_args[] = {
        {
            .name = SV("-o"),
            .altname1 = SV("--output"),
            .dest = ARGDEST(&dst),
            .min_num = 0, .max_num = 1,
            .help = "output html file",
        },
        {
            .name = SV("-s"),
            .altname1 = SV("--stylesheet"),
            .dest = ARGDEST(&stylesheet),
            .min_num = 0, .max_num = 1,
            .help = "stylesheet to append to the output",
        },
        #ifdef EMBEDDED_STYLESHEET
        {
            .name = SV("--no-stylesheet"),
            .dest = ARGDEST(&no_stylesheet),
            .help = "Don't add a <style> tag",
        },
        #endif
    };
    enum {HELP, VERSION, FISH};
    ArgToParse early_args[] = {
        [HELP] = {
            .name = SV("-h"),
            .altname1 = SV("--help"),
            .help = "Print this help and exit.",
        },
        [VERSION] = {
            .name = SV("-v"),
            .altname1 = SV("--version"),
            .help = "Print version information and exit.",
        },
        [FISH] = {
            .name = SV("--fish-completions"),
            .help = "Print out commands for fish shell completions.",
            .hidden = 1,
        },
    };
    ArgParser parser = {
        .name = argc? argv[0]: "drmd",
        .description = "markdown parser",
        .positional = {
            .args = pos_args,
            .count = arrlen(pos_args),
        },
        .keyword = {
            .args = kw_args,
            .count = arrlen(kw_args),
        },
        .early_out = {
            .args = early_args,
            .count = arrlen(early_args),
        },
        .styling = {.plain = !isatty(fileno(stdout))},
    };
    const char* version = "drmd version 1.0";
    Args args = {argc-1, argv+1};
    switch(check_for_early_out_args(&parser, &args)){
        case HELP:{
            int columns = get_terminal_size().columns;
            if(columns > 80) columns = 80;
            print_argparse_help(&parser, columns);
            return 0;
        }
        case VERSION:
            puts(version);
            return 0;
        case FISH:{
            print_argparse_fish_completions(&parser);
            return 0;
        }
        default:
            break;
    }
    enum ArgParseError error = parse_args(&parser, &args, 0);
    if(error){
        print_argparse_error(&parser, error);
        return error;
    }
    FILE* inp = stdin;
    if(src.text){
        inp = fopen(src.text, "rb");
        if(!inp){
            fprintf(stderr, "Unable to open '%s': %s\n", src.text, strerror(errno));
            return 1;
        }
    }
    MStringBuilder sb = {.allocator=MALLOCATOR};
    for(;;){
        int e = msb_ensure_additional(&sb, 1024);
        if(e) return 1;
        char* buff = sb.data + sb.cursor;
        size_t nread = fread(buff, 1, 1024, inp);
        sb.cursor += nread;
        if(nread != 1024){
            if(ferror(inp)){
                fprintf(stderr, "Error reading: %s\n", strerror(errno));
                return 1;
            }
            else
                break;
        }
    }
    StringView txt = msb_detach_sv(&sb);
    StringView md;
    int err = drmd_to_html(txt, &md);
    if(err) return err;
    FILE* output = stdout;
    if(dst.length){
        output = fopen(dst.text, "wb");
        if(!output){
            fprintf(stderr, "Unable to open '%s': %s\n", dst.text, strerror(errno));
            return 1;
        }
    }
    {
        size_t nwrit = fwrite(md.text, md.length, 1, output);
        if(nwrit != 1){
            if(dst.length)
                fprintf(stderr, "Error writing to '%s': %s\n", dst.text, strerror(errno));
            else
                fprintf(stderr, "Error writing: %s\n", strerror(errno));
        }
    }
    if(no_stylesheet){
        // do nothing
    }
    else if(stylesheet.length){
        FILE* s = fopen(stylesheet.text, "rb");
        if(!s){
            fprintf(stderr, "Unable to read stylesheet '%s': %s\n", stylesheet.text, strerror(errno));
        }
        else {
            char buff[4000];
            _Bool loop = 1;
            for(;loop;){
                size_t nread = fread(buff, 1, sizeof buff, s);
                if(nread != sizeof buff){
                    if(ferror(s)){
                        fprintf(stderr, "Error reading '%s': %s\n", stylesheet.text, strerror(errno));
                        return 1;
                    }
                    loop = 0;
                }
                size_t nwrit = fwrite(buff, nread, 1, output);
                if(nwrit != 1){
                    fprintf(stderr, "Error writing to output: %s\n", strerror(errno));
                    return 1;
                }
            }
            fclose(s);
        }
    }
    else {
        #ifdef EMBEDDED_STYLESHEET
        fprintf(output, "\n%s\n", _readme_stylesheet);
        #endif
    }
    fflush(output);
    fclose(output);
    return 0;
}

#include "drmd.c"
#include "Allocators/allocator.c"
