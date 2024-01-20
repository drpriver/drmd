# DrMd

This is a library for compiling a subset of a md-like format to html.
The main intention is for it to be easy to compile to wasm and included
on webpages for converting user-created content to html.

It doesn't support all of markdown and skips the silly "loose and tight" lists
nonsense.

Also, tables don't have explicit header delimiter line.

## TODO

- fuzzing
- links
- ensure inline tags (b, i, etc.) are closed.

## Won't support

- Arbitrary HTML
- Emphasis markers (just use b tags or whatever).

## Wasm
You need a clang (not apple-clang) to compile to wasm. If you have that, then:

```
$ make Bin/drmd.wasm
```

Will compile to a wasm object that exports a <tt>make_html</tt> func (see drmd_wasm.c).
