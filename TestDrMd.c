//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#ifdef _WIN32
#define _CRT_NONSTDC_NO_WARNINGS 1
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include "drmd.h"
#define PARSE_NUMBER_PARSE_FLOATS 0
#include "testing.h"
#include "stringview.h"
#define REPLACE_MALLOCATOR 1
#define USE_TESTING_ALLOCATOR 1
#include "Allocators/testing_allocator.h"
#include "Allocators/mallocator.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

static TestFunc TestMd;

int main(int argc, char*_Null_unspecified*_Null_unspecified argv){
    if(!test_funcs_count){ // wasm calls main more than once.
        RegisterTest(TestMd);
    }
    int ret = test_main(argc, argv, NULL);
    return ret;
}

#ifndef arrlen
#define arrlen(x) (sizeof(x)/sizeof(x[0]))
#endif

TestFunction(TestMd){
    TESTBEGIN();
    struct {
        StringView input;
        StringView expected;
    } test_cases[] = {
        {
            SV(">a\nb\n"),
            SV("<blockquote>\na\nb</blockquote>\n"),
        },
        {
            SV(
            "- foo\n"
            "bar\n"
            ),
            SV(
                "<ul>\n<li>foo</ul>\n<p>bar"
            ),
        },
        {
            SV(
            "- foo\n"
            ),
            SV(
                "<ul>\n<li>foo</ul>\n"
            ),
        },
        {
            SV(
            "- foo\n"
            "  bar\n"
            ),
            SV(
                "<ul>\n<li>foo bar</ul>\n"
            ),
        },
        {
            SV(
            "- foo\n"
            "  bar\n"
            " - baz\n"
            ),
            SV(
                "<ul>\n<li>foo bar <ul>\n<li>baz</ul>\n</ul>\n"
            ),
        },
        {
            SV(
            "> foo\n"
            "> bar\n"
            "> baz\n"
            ),
            SV(
                "<blockquote>\nfoo\nbar\nbaz</blockquote>\n"
            ),
        },
        {
            SV(
            "```\n"
            "> foo\n"
            "> bar\n"
            "> baz\n"
            "```\n"
            ),
            SV(
                "<pre>&gt; foo\n&gt; bar\n&gt; baz\n</pre>\n"
            ),
        },
        {
            SV(
            "|hello|world\n"
            "|foo | bar\n"
            ),
            SV(
                "<table>\n<thead>\n<tr>\n<th>hello<th>world\n<tbody>\n<tr><td>foo<td>bar</table>\n"
            ),
        },
        {
            SV(
            "- foo\n"
            "#hello\n"
            "- bar\n"
            ),
            SV(
                "<ul>\n<li>foo</ul>\n<h1>hello</h1>\n<ul>\n<li>bar</ul>\n"
            ),
        },
        {
            SV("|foo\n"
               "a\n"),
            SV( "<table>\n<thead>\n<tr>\n<th>foo\n<tbody>\n</table>\n<p>a"),
        },
        {
            SV("  - a\n"
               "- b\n"),
            SV(
                "<ul>\n<li>a</ul>\n"
                "<ul>\n<li>b</ul>\n"
              ),
        },
        {
            SV(
                "+ a\n"
                "  o b\n"
                " o c\n"
            ),
            SV(
                "<ul>\n<li>a <ul>\n<li>b</ul>\n</ul>\n<ul>\n<li>c</ul>\n"
            ),
        },
    };
    for(size_t i = 0; i < arrlen(test_cases); i++){
        StringView out;
        int e = drmd_to_html(test_cases[i].input, &out);
        TestAssertFalse(e);
        TestExpectEquals2(sv_equals, out, test_cases[i].expected);
        Allocator_free(MALLOCATOR, out.text, out.length);
        testing_assert_all_freed();
    }
    TESTEND();
}


#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#include "drmd.c"
