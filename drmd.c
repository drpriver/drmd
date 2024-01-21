//
// Copyright © 2024, David Priver <david@davidpriver.com>
//
#include <stdint.h>
#include "drmd.h"
#include "stringview.h"
#include "Allocators/arena_allocator.h"
#include "Allocators/mallocator.h"
#include "MStringBuilder.h"

// simd includes
#ifndef NO_SIMD
#ifdef __x86_64__
#include <immintrin.h>
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

#endif

#ifdef __clang__
#pragma clang assume_nonnull begin
#else
#ifndef _Nullable
#define _Nullable
#endif
#ifndef _Nonnull
#define _Nonnull
#endif
#endif


//
// Macros
//
#ifndef force_inline
#if defined(__GNUC__) || defined(__clang__)
#define force_inline static inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define force_inline static inline __forceinline
#else
#define force_inline static inline
#endif
#endif

#if !defined(likely) && !defined(unlikely)
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define warn_unused __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define warn_unused
#else
#define warn_unused
#endif

#ifndef arrlen
#define arrlen(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

#ifndef CASE_0_9
#define CASE_0_9 '0': case '1': case '2': case '3': case '4': case '5': \
    case '6': case '7': case '8': case '9'
#endif

enum { ERROR_OOM = 1, };

static inline
StringView
stripped_view(const char* str, size_t len){
    for(;len;len--, str++){
        switch(*str){
            case ' ': case '\t': case '\r': case '\n': case '\f': case '\v':
                continue;
            default:
                break;
        }
        break;
    }
    for(;len;len--){
        switch(str[len-1]){
            case ' ': case '\t': case '\r': case '\n': case '\f': case '\v':
                continue;
            default:
                break;
        }
        break;
    }
    return (StringView){.length=len, .text=str};
}


typedef union NodeHandle NodeHandle;
union NodeHandle {
    struct { uint32_t index; };
    uint32_t _value;
};
enum {INVALID_NODE_HANDLE_VALUE = -1};
#define INVALID_NODE_HANDLE ((NodeHandle){._value=INVALID_NODE_HANDLE_VALUE})

force_inline
_Bool
NodeHandle_eq(NodeHandle a, NodeHandle b){return a._value == b._value;}

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#define RARRAY_T NodeHandle
#include "Rarray.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#define NODETYPES(apply) \
    apply(INVALID,    0) \
    apply(MD,         1) \
    apply(STRING,     2) \
    apply(PARA,       3) \
    apply(TABLE,      4) \
    apply(TABLE_ROW,  5) \
    apply(BULLETS,    6) \
    apply(LIST,       7) \
    apply(LIST_ITEM,  8) \
    apply(QUOTE,      9) \
    apply(PRE,       10) \
    apply(H,         11) \

enum NodeType{
#define apply(a, b) NODE_##a = b,
    NODETYPES(apply)
#undef apply
};

typedef enum NodeType NodeType;

typedef struct Node Node;
struct Node{
    // The type of the node
    NodeType type;                // 4 bytes
    // Handle to this node's parent node.
    // It is possible for this to be INVALID_NODE_HANDLE, which indicates it
    // has no parent.
    // NodeHandle parent;            // 4 bytes
    // The header text for a node.
    // For NODE_STRING, this is instead the contents of that node
    StringView header;            // 16 bytes
    // Handles to child nodes.
    union {
        Rarray(NodeHandle)*_Nullable children; // 8 bytes
        int heading_level;
    };
};

force_inline
NodeHandle*_Nullable
node_children(Node* node){
    if(node->children)
        return node->children->data;
    else
        return NULL;
}

force_inline
size_t
node_children_count(Node* node){return node->children?node->children->count:0;}

#define NODE_CHILDREN_FOR_EACH(iter, n) \
    RARRAY_FOR_EACH(NodeHandle, iter, (n)->children)

#ifdef __clang__
#pragma clang assume_nonnull end
#endif

#define MARRAY_T Node
#include "Marray.h"

#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

typedef struct ParseLocation ParseLocation;
struct ParseLocation {
    const char*_Nonnull cursor;
    const char*_Nonnull end;
    const char*_Null_unspecified line_start;
    const char*_Null_unspecified line_end;
    int nspaces;
};


static inline
void
analyze_line(ParseLocation*);

force_inline
void
advance_row(ParseLocation* loc){
    if(unlikely(loc->line_end == loc->end))
        loc->cursor = loc->line_end;
    else
        loc->cursor = loc->line_end+1;
    // ctx->lineno++;
}

typedef struct DrMdContext DrMdContext;
struct DrMdContext {
    // The actual storage for all the nodes.
    Marray(Node) nodes;

    // General purpose allocator.
    ArenaAllocator main_arena;
};

force_inline
Allocator
main_allocator(DrMdContext* ctx){
    return allocator_from_arena(&ctx->main_arena);
}

force_inline
NodeHandle
alloc_handle_(DrMdContext* ctx, NodeType type){
    size_t index; int err = Marray_alloc_index(Node)(&ctx->nodes, main_allocator(ctx), &index);
    if(unlikely(err))
        return INVALID_NODE_HANDLE;
    ctx->nodes.data[index] = (Node){.type=type};
    return (NodeHandle){.index=index};
}

force_inline
NodeHandle
alloc_string(DrMdContext* ctx, StringView sv){
    size_t index; int err = Marray_alloc_index(Node)(&ctx->nodes, main_allocator(ctx), &index);
    if(unlikely(err))
        return INVALID_NODE_HANDLE;
    ctx->nodes.data[index] = (Node){.type=NODE_STRING, .header=sv};
    return (NodeHandle){.index=index};
}

force_inline
Node*
get_node(DrMdContext* ctx, NodeHandle handle){
    assert(handle.index < ctx->nodes.count);
    Node* result = &ctx->nodes.data[handle.index];
    return result;
}

force_inline
warn_unused
int
append_child_(DrMdContext* ctx, NodeHandle parent_handle, NodeHandle child_handle){
    Node* parent = get_node(ctx, parent_handle);
    int err = Rarray_push(NodeHandle)(&parent->children, main_allocator(ctx), child_handle);
    if(unlikely(err))
        return ERROR_OOM;
    return 0;
}

force_inline
warn_unused
NodeHandle
append_node(DrMdContext* ctx, NodeHandle parent, NodeType type){
    NodeHandle handle = alloc_handle_(ctx, type);
    if(NodeHandle_eq(handle, INVALID_NODE_HANDLE))
        return handle;
    int e = append_child_(ctx, parent, handle);
    if(e) return INVALID_NODE_HANDLE;
    return handle;
}

force_inline
warn_unused
NodeHandle
append_string(DrMdContext* ctx, NodeHandle parent, StringView sv){
    NodeHandle handle = alloc_string(ctx, sv);
    if(NodeHandle_eq(handle, INVALID_NODE_HANDLE))
        return handle;
    int e = append_child_(ctx, parent, handle);
    if(e) return INVALID_NODE_HANDLE;
    return handle;
}

static
int
parse_md_node(DrMdContext* ctx, ParseLocation* loc, NodeHandle parent_handle);

static
int
render_to_html(DrMdContext* ctx, NodeHandle root, MStringBuilder* msb);


DRMD_API
int
drmd_to_html(StringView input, StringView* output){
    DrMdContext ctx = {0};
    ParseLocation loc = {
        .cursor = input.text,
        .end = input.text + input.length,
    };
    NodeHandle root = alloc_handle_(&ctx, NODE_MD);
    if(NodeHandle_eq(root, INVALID_NODE_HANDLE))
        return ERROR_OOM;
    int err = parse_md_node(&ctx, &loc, root);
    if(err) goto cleanup;
    MStringBuilder msb = {.allocator = MALLOCATOR};
    err = render_to_html(&ctx, root, &msb);
    if(!err){
        if(!msb.cursor){
            msb_destroy(&msb);
            *output = (StringView){0};
        }
        else
            *output = msb_detach_sv(&msb);
    }
    else {
        msb_destroy(&msb);
    }
    cleanup:
    ArenaAllocator_free_all(&ctx.main_arena);
    return err;
}

static
int
parse_md_node(DrMdContext* ctx, ParseLocation* loc, NodeHandle parent_handle){
    enum MDSTATE {
        NONE = 0,
        PARA = 1,
        BULLET = 2,
        LIST = 3,
        TABLE = 4,
        QUOTE = 5,
    };
    enum MDSTATE state = NONE;
    struct StackItem {
        NodeHandle list;
        NodeHandle item;
        int indentation;
        enum MDSTATE state;
    } stack[16];
    int si = -1; // stack index
    NodeHandle container_handle = INVALID_NODE_HANDLE;
    int normal_indent = -1;
    for(;loc->cursor != loc->end;){
        analyze_line(loc);
        // skip_blanks
        if(loc->line_start+loc->nspaces == loc->line_end){
            state = NONE;
            si = -1;
            advance_row(loc);
            continue;
        }
        if(normal_indent < 0){
            normal_indent = loc->nspaces;
        }
        enum MDSTATE newstate = NONE;
        const char* firstchar = loc->line_start + loc->nspaces;
        int prefix_length = 0;
        switch(*firstchar){
            // "•"
            case '\xe2':
                if(firstchar+3 < loc->end && firstchar[1] == '\x80' && firstchar[2] == '\xa2' && firstchar[3] == ' '){
                    prefix_length = 4;
                    newstate = BULLET;
                }
                else
                    newstate = PARA;
                goto after;
            case '+':
            case '-':
            case '*':
            case 'o':
                if(firstchar+1 != loc->end && firstchar[1] == ' '){
                    prefix_length = 1;
                    newstate = BULLET;
                }
                else
                    newstate = PARA;
                goto after;
            case '#':{
                int h = 1;
                firstchar++;
                for(;firstchar != loc->end && *firstchar=='#';firstchar++)
                    h++;
                NodeHandle heading = append_node(ctx, parent_handle, NODE_H);
                if(NodeHandle_eq(heading, INVALID_NODE_HANDLE))
                    return ERROR_OOM;
                Node* n = get_node(ctx, heading);
                n->heading_level = h;
                n->header = (StringView){loc->line_end-firstchar, firstchar};
                advance_row(loc);
                state = NONE;
                si = -1;
                continue;
            }
            case CASE_0_9:{
                prefix_length = 1;
                newstate = PARA;
                for(const char* c = firstchar+1;c != loc->end;c++){
                    switch(*c){
                        case CASE_0_9:
                            prefix_length++;
                            continue;
                        case '.':
                            prefix_length++;
                            newstate = LIST;
                            goto after;
                        default:
                            goto after;
                    }
                }
            }break;
            case '`':{
                if(loc->line_end - firstchar == 3){
                    if(firstchar[1] == '`' && firstchar[2] == '`'){
                        NodeHandle pre = append_node(ctx, parent_handle, NODE_PRE);
                        if(NodeHandle_eq(pre, INVALID_NODE_HANDLE))
                            return ERROR_OOM;
                        advance_row(loc);
                        for(;loc->cursor != loc->end;){
                            analyze_line(loc);
                            firstchar = loc->line_start + loc->nspaces;
                            if(loc->line_end - firstchar == 3){
                                if(firstchar[1] == '`' && firstchar[2] == '`'){
                                    advance_row(loc);
                                    state = NONE;
                                    si = -1;
                                    break;
                                }
                            }
                            NodeHandle string = append_string(ctx, pre, (StringView){ loc->line_end-loc->line_start, loc->line_start});
                            if(NodeHandle_eq(string, INVALID_NODE_HANDLE))
                                return ERROR_OOM;
                            advance_row(loc);
                        }
                        continue;
                    }
                }
            }
            // fall-through
            default:
                newstate = PARA;
                goto after;
            case '|':
                newstate = TABLE;
                goto after;
            case '>':
                newstate = QUOTE;
                goto after;

        }
        after:;
        assert(newstate != NONE);
        if(newstate == BULLET || newstate == LIST){
            if(si == -1){
                si = 0;
                struct StackItem* s = &stack[si];
                s->list = append_node(ctx, parent_handle, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                    return ERROR_OOM;
                s->item = INVALID_NODE_HANDLE;
                s->indentation = loc->nspaces;
                s->state = newstate;
            }
            else {
                // new level of list
                if(loc->nspaces > stack[si].indentation){
                    si++;
                    if(si == arrlen(stack)){
                        return ERROR_OOM;
                    }
                    struct StackItem* s = &stack[si];
                    assert(si > 0);
                    s->list = append_node(ctx, stack[si-1].item, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                    if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                        return ERROR_OOM;
                    s->item = INVALID_NODE_HANDLE;
                    s->indentation = loc->nspaces;
                    s->state = newstate;
                }
                // neighbors
                else if(loc->nspaces == stack[si].indentation){
                    struct StackItem* s = &stack[si];
                    if(s->state != newstate){
                        // neighbor of different type
                        NodeHandle prev = si>0? stack[si-1].item : parent_handle;
                        s->list = append_node(ctx, prev, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                        if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                            return ERROR_OOM;
                        s->item = INVALID_NODE_HANDLE;
                        s->indentation = loc->nspaces;
                        s->state = newstate;
                    }
                    else {
                        // Neighbor of same type, do nothing
                    }
                }
                // go back up
                else {
                    for(;;){
                        si--;
                        if(si < 0){
                            si = 0;
                            struct StackItem* s = &stack[si];
                            s->list = append_node(ctx, parent_handle, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                            if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                                return ERROR_OOM;
                            s->item = INVALID_NODE_HANDLE;
                            s->indentation = loc->nspaces;
                            s->state = newstate;
                            goto after_go_up;
                        }
                        assert(si >= 0);
                        int indent = stack[si].indentation;
                        if(indent > loc->nspaces)
                            continue;
                        if(indent == loc->nspaces)
                            break;
                        if(indent < loc->nspaces){
                            si = 0;
                            struct StackItem* s = &stack[si];
                            s->list = append_node(ctx, parent_handle, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                            if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                                return ERROR_OOM;
                            s->item = INVALID_NODE_HANDLE;
                            s->indentation = loc->nspaces;
                            s->state = newstate;
                            goto after_go_up;
                        }
                    }
                    struct StackItem* s = &stack[si];
                    if(s->state != newstate){
                        s->list = append_node(ctx, si?stack[si-1].item:parent_handle, newstate==BULLET?NODE_BULLETS:NODE_LIST);
                        if(unlikely(NodeHandle_eq(s->list, INVALID_NODE_HANDLE)))
                            return ERROR_OOM;
                        s->item = INVALID_NODE_HANDLE;
                        s->indentation = loc->nspaces;
                        s->state = newstate;
                    }
                }
            }
            after_go_up:;
            struct StackItem* s = &stack[si];
            s->item = append_node(ctx, s->list, NODE_LIST_ITEM);
            if(unlikely(NodeHandle_eq(s->item, INVALID_NODE_HANDLE)))
                return ERROR_OOM;
            StringView content = stripped_view(loc->line_start + loc->nspaces+prefix_length, (loc->line_end - loc->line_start)-loc->nspaces-prefix_length);
            NodeHandle new_node_handle = append_string(ctx, s->item, content);
            if(unlikely(NodeHandle_eq(new_node_handle, INVALID_NODE_HANDLE)))
                return ERROR_OOM;
            advance_row(loc);
            state = newstate;
            continue;
        }
        if(newstate == TABLE){
            if(state != TABLE){
                container_handle = append_node(ctx, parent_handle, NODE_TABLE);
                if(unlikely(NodeHandle_eq(container_handle, INVALID_NODE_HANDLE)))
                    return ERROR_OOM;
            }
            NodeHandle table_row_handle = append_node(ctx, container_handle, NODE_TABLE_ROW);
            if(unlikely(NodeHandle_eq(table_row_handle, INVALID_NODE_HANDLE)))
                return ERROR_OOM;
            const char* p = firstchar+1;
            for(;;){
                const char* pi = memchr(p, '|', loc->line_end - p);
                if(!pi){
                    ptrdiff_t diff = loc->line_end - p;
                    NodeHandle content = append_string(ctx, table_row_handle, stripped_view(p, diff));
                    if(unlikely(NodeHandle_eq(content, INVALID_NODE_HANDLE)))
                        return ERROR_OOM;
                    break;
                }
                ptrdiff_t diff = pi - p;
                NodeHandle content = append_string(ctx, table_row_handle, stripped_view(p, diff));
                if(unlikely(NodeHandle_eq(content, INVALID_NODE_HANDLE)))
                    return ERROR_OOM;
                p = pi+1;
            }
            advance_row(loc);
            state = newstate;
            si = -1;
            continue;
        }
        if(newstate == QUOTE){
            if(state != QUOTE){
                container_handle = append_node(ctx, parent_handle, NODE_QUOTE);
                if(NodeHandle_eq(container_handle, INVALID_NODE_HANDLE))
                    return ERROR_OOM;
                si = -1;
            }
            NodeHandle string = append_string(ctx, container_handle, stripped_view(loc->line_start+1, loc->line_end-loc->line_start-1));
            if(NodeHandle_eq(string, INVALID_NODE_HANDLE))
                return ERROR_OOM;
            advance_row(loc);
            state = newstate;
            continue;
        }
        assert(newstate == PARA);
        if(state == QUOTE){
            StringView content = stripped_view( loc->line_start + loc->nspaces, (loc->line_end - loc->line_start)-loc->nspaces);
            NodeHandle new_node_handle = append_string(ctx, container_handle, content);
            if(unlikely(NodeHandle_eq(new_node_handle, INVALID_NODE_HANDLE)))
                return ERROR_OOM;
            advance_row(loc);
            continue;
        }
        if(state == PARA || state == NONE || loc->nspaces == normal_indent || state == TABLE){
            if(state != PARA){
                container_handle = append_node(ctx, parent_handle, NODE_PARA);
                if(unlikely(NodeHandle_eq(container_handle, INVALID_NODE_HANDLE)))
                    return ERROR_OOM;
            }
            StringView content = stripped_view( loc->line_start + loc->nspaces, (loc->line_end - loc->line_start)-loc->nspaces);
            NodeHandle new_node_handle = append_string(ctx, container_handle, content);
            if(unlikely(NodeHandle_eq(new_node_handle, INVALID_NODE_HANDLE)))
                return ERROR_OOM;
            advance_row(loc);
            si = -1;
            state = newstate;
            continue;
        }
        StringView content = stripped_view(loc->line_start + loc->nspaces, (loc->line_end - loc->line_start)-loc->nspaces);
        NodeHandle new_node_handle = append_string(ctx, stack[si].item, content);
        if(unlikely(NodeHandle_eq(new_node_handle, INVALID_NODE_HANDLE)))
            return ERROR_OOM;
        advance_row(loc);
        continue;
    }
    return 0;
}

#if defined(__GNUC__) || defined(__clang__)
#define unused_param __attribute__((__unused__))
#else
#define unused_param
#endif

#define RENDERFUNCNAME(nt) render_##nt
#define RENDERFUNC(nt) static warn_unused int RENDERFUNCNAME(nt)(DrMdContext* ctx, MStringBuilder* sb, NodeHandle handle, int unused_param node_depth)

#define X(a, b) RENDERFUNC(a);
NODETYPES(X)
#undef X

typedef int(renderfunc)(DrMdContext*, MStringBuilder*, NodeHandle, int);

static
renderfunc*_Nonnull const RENDERFUNCS[] = {
    #define X(a,b) [NODE_##a] = &RENDERFUNCNAME(a),
    NODETYPES(X)
    #undef X
};

force_inline
warn_unused
int
render_node(DrMdContext* ctx, MStringBuilder* restrict sb, NodeHandle handle, int node_depth){
    enum {MAX_NODE_DEPTH=20};
    if(node_depth > MAX_NODE_DEPTH) return 1;
    Node* node = get_node(ctx, handle);
    return RENDERFUNCS[node->type](ctx, sb, handle, node_depth+1);
}

static
int
render_to_html(DrMdContext* ctx, NodeHandle root, MStringBuilder* msb){
    // estimate memory usage as 120 characters per node.
    size_t reserve_amount = ctx->nodes.count*120;
    int err = msb_ensure_additional(msb, reserve_amount);
    if(err) return ERROR_OOM;
    int e = render_node(ctx, msb, root, 0);
    return e;
}

static inline
int
write_link_escaped_str(MStringBuilder* sb, const char* text, size_t length);

RENDERFUNC(STRING){
    Node* node = get_node(ctx, handle);
    int e = write_link_escaped_str(sb, node->header.text, node->header.length);
    if(e) return e;
    // msb_write_char(sb, '\n');
    return 0;
}
RENDERFUNC(INVALID){
    (void)ctx;
    (void)sb;
    (void)handle;
    return -1;
}
RENDERFUNC(PARA){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<p>");
    _Bool first = 1;
    NODE_CHILDREN_FOR_EACH(it, node){
        if(!first) msb_write_char(sb, '\n');
        first = 0;
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
    }
    // closing </p> is not needed
    // msb_write_literal(sb, "</p>\n");
    return 0;
}
RENDERFUNC(BULLETS){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<ul>\n");
    NODE_CHILDREN_FOR_EACH(it, node){
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
    }
    msb_write_literal(sb, "</ul>\n");
    return 0;
}
RENDERFUNC(LIST){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<ol>\n");
    NODE_CHILDREN_FOR_EACH(it, node){
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
    }
    msb_write_literal(sb, "</ol>\n");
    return 0;
}
RENDERFUNC(LIST_ITEM){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<li>");
    size_t count = node_children_count(node);
    NodeHandle* children = node_children(node);
    for(size_t i = 0; i < count; i++){
        if(i != 0)
            msb_write_char(sb, ' ');
        int e = render_node(ctx, sb, children[i], node_depth);
        if(e) return e;
    }
    // closing </li> is not needed
    // msb_write_literal(sb, "</li>\n");
    return 0;
}
RENDERFUNC(MD){
    Node* node = get_node(ctx, handle);
    NODE_CHILDREN_FOR_EACH(it, node){
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
    }
    return 0;
}

RENDERFUNC(TABLE){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<table>\n<thead>\n");
    size_t count = node_children_count(node);
    NodeHandle* children = node_children(node);
    if(count){
        Node* child = get_node(ctx, children[0]);
        assert(child->type == NODE_TABLE_ROW);
        // inline rendering table row here so we can do heads
        msb_write_literal(sb, "<tr>\n");
        NODE_CHILDREN_FOR_EACH(it, child){
            msb_write_literal(sb, "<th>");
            int e = render_node(ctx, sb, *it, node_depth);
            if(e) return e;
            // closing </th> is not needed
            // msb_write_literal(sb, "</th>\n");
        }
        // closing </tr> is not needed
        // msb_write_literal(sb, "</tr>\n");
    }
    msb_write_literal(sb, "\n<tbody>\n");
    // <tbody> is not required
    for(size_t i = 1; i < count; i++){
        int e = render_node(ctx, sb, children[i], node_depth);
        if(e) return e;
    }
    msb_write_literal(sb, "</table>\n");
    return 0;
}
RENDERFUNC(TABLE_ROW){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<tr>");
    NODE_CHILDREN_FOR_EACH(it, node){
        msb_write_literal(sb, "<td>");
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
        // closing </td> is not needed
        // msb_write_literal(sb, "</td>");
    }
    // msb_write_char(sb, '\n');
    // closing </tr> is not needed
    // msb_write_literal(sb, "</tr>\n");
    return 0;
}
RENDERFUNC(QUOTE){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<blockquote>\n");
    _Bool first = 1;
    NODE_CHILDREN_FOR_EACH(it, node){
        if(!first) msb_write_char(sb, '\n');
        first = 0;
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
    }
    msb_write_literal(sb, "</blockquote>\n");
    return 0;
}
RENDERFUNC(PRE){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<pre>");
    NODE_CHILDREN_FOR_EACH(it, node){
        int e = render_node(ctx, sb, *it, node_depth);
        if(e) return e;
        msb_write_char(sb, '\n');
    }
    msb_write_literal(sb, "</pre>\n");
    return 0;
}

RENDERFUNC(H){
    Node* node = get_node(ctx, handle);
    msb_write_literal(sb, "<h");
    msb_write_char(sb, '0'+node->heading_level);
    msb_write_char(sb, '>');
    int e = write_link_escaped_str(sb, node->header.text, node->header.length);
    if(e) return e;
    msb_write_literal(sb, "</h");
    msb_write_char(sb, '0'+node->heading_level);
    msb_write_char(sb, '>');
    msb_write_char(sb, '\n');
    return 0;
}

static inline
int
write_link_escaped_str_slow(MStringBuilder* sb, const char* text, size_t length){
    for(size_t i = 0; i < length; i++){
        char c = text[i];
        switch(c){
            case '-':{
                if(i < length - 1){
                    char peek1 = text[i+1];
                    if(peek1 == '-'){
                        if(i < length - 2){
                            char peek2 = text[i+2];
                            if(peek2 == '-'){
                                msb_write_literal(sb, "&mdash;");
                                i += 2;
                                continue;
                            }
                        }
                        msb_write_literal(sb, "&ndash;");
                        i += 1;
                        continue;
                    }
                }
                msb_write_char(sb, c);
            }break;
            case '&':{ // allow &lt;, &gt;
                if(length - i >= 4){
                    if(memcmp(text+i, "&lt;", 4) == 0){
                        msb_write_literal(sb, "&lt;");
                        i += 3;
                        continue;
                    }
                    if(memcmp(text+i, "&gt;", 4) == 0){
                        msb_write_literal(sb, "&gt;");
                        i += 3;
                        continue;
                    }
                }
                msb_write_literal(sb, "&amp;");
            }break;
            case '<':{
                // we allow inline <b>, <s>, <i>, </b>, </s>, </i>, <br>, <code>, </code>, <hr>, <tt>, </tt>, <u>, </u>
                // This is a big mess and should be done in an easier to do way.
                if(length - i >= 2){
                    char peek1 = text[i+1];
                    if(peek1 == 'c'){ // could be <code> tag
                        if(length - i >= sizeof("<code>")-1){
                            if(memcmp(text+i, "<code>", sizeof("<code>")-1) == 0){
                                msb_write_literal(sb, "<code>");
                                i += sizeof("<code>")-2;
                                continue;
                            }
                        }
                    }
                    if(peek1 == 'h'){
                        if(length - i >= sizeof("<hr>")-1){
                            if(memcmp(text+i, "<hr>", sizeof("<hr>")-1) == 0){
                                msb_write_literal(sb, "<hr>");
                                i += sizeof("<hr>")-2;
                                continue;
                            }
                        }
                    }
                    if(peek1 == '/'){
                        if(length - i >= sizeof("</code>")-1){
                            if(memcmp(text+i, "</code>", sizeof("</code>")-1) == 0){
                                msb_write_literal(sb, "</code>");
                                i += sizeof("</code>")-2;
                                continue;
                            }
                        }
                        if(length - i >= sizeof("</tt>")-1){
                            if(memcmp(text+i, "</tt>", sizeof("</tt>")-1) == 0){
                                msb_write_literal(sb, "</tt>");
                                i += sizeof("</tt>")-2;
                                continue;
                            }
                        }
                    }
                    if(peek1 == 't'){
                        if(length - i >= sizeof("<tt>")-1){
                            if(memcmp(text+i, "<tt>", sizeof("<tt>")-1) == 0){
                                msb_write_literal(sb, "<tt>");
                                i += sizeof("<tt>")-2;
                                continue;
                            }
                        }
                    }
                    switch(peek1){
                        case 'b':
                        case 's':
                        case 'i':
                        case 'u':
                        case '/':
                            break;
                        default:
                            msb_write_literal(sb, "&lt;");
                            continue;
                    }
                    if(length - i >= 3){
                        char peek2 = text[i+2];
                        if(peek1 == 'b' && peek2 == 'r'){
                            if(length - i >= 4 && text[i+3] == '>'){
                                msb_write_literal(sb, "<br>");
                                i += sizeof("<br>")-2;
                                continue;
                            }
                        }
                        if(peek1 != '/'){
                            if(peek2 == '>'){
                                msb_write_char(sb, c);
                                msb_write_char(sb, peek1);
                                msb_write_char(sb, peek2);
                                i += 2;
                                continue;
                            }
                            msb_write_literal(sb, "&lt;");
                            continue;
                        }
                        switch(peek2){
                            case 'b':
                            case 's':
                            case 'i':
                            case 'u':
                                break;
                            default:
                                msb_write_literal(sb, "&lt;");
                                continue;
                        }
                        if(length -i >= 4){
                            char peek3 = text[i+3];
                            if(peek3 == '>'){
                                msb_write_char(sb, c);
                                msb_write_char(sb, peek1);
                                msb_write_char(sb, peek2);
                                msb_write_char(sb, peek3);
                                i += 3;
                                continue;
                            }
                        }
                    }
                }
                msb_write_literal(sb, "&lt;");
            }break;
            case '>':{
                msb_write_literal(sb, "&gt;");
            }break;
            case '\r':
            case '\f':{
                msb_write_char(sb, ' ');
            }break;
            // Don't print control characters.
            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
            case 10: case 11:
            // This would've been so much nicer!
            // case 14 ... 31:
            case 14: case 15: case 16: case 17: case 18: case 19: case 20:
            case 21: case 22: case 23: case 24: case 25: case 26: case 27:
            case 28: case 29: case 30: case 31:
                break;
            default:
                msb_write_char(sb, c);
                break;
        }
    }
    return 0;
}

static inline
int
write_link_escaped_str(MStringBuilder* sb, const char* text, size_t length){
    int err = msb_ensure_additional(sb, length);
    if(unlikely(err))
        return ERROR_OOM;
#if 1 && !defined(NO_SIMD) && defined(__x86_64__)
    size_t cursor = sb->cursor;
    char* sbdata = sb->data + cursor;
    __m128i lsquare = _mm_set1_epi8('[');
    __m128i hyphen  = _mm_set1_epi8('-');
    __m128i langle  = _mm_set1_epi8('<');
    __m128i rangle  = _mm_set1_epi8('>');
    __m128i amp     = _mm_set1_epi8('&');
    __m128i control = _mm_set1_epi8(32);
    while(length >= 16){
        // This code is straightforward. Check each 16byte chunk for the
        // presence of one of the special characters.  Also check for control
        // characters (ascii < 32), as those are not valid in html, with the
        // exception of newline.
        //
        // For the common case of no special character this is much faster
        // than the byte at a time processing we'd otherwise have to do.
        __m128i data         = _mm_loadu_si128((const __m128i*)text);
        __m128i test_lsquare = _mm_cmpeq_epi8(data, lsquare);
        __m128i test_hyphen  = _mm_cmpeq_epi8(data, hyphen);
        __m128i test_langle  = _mm_cmpeq_epi8(data, langle);
        __m128i test_rangle  = _mm_cmpeq_epi8(data, rangle);
        __m128i test_amp     = _mm_cmpeq_epi8(data, amp);
        __m128i test_control = _mm_cmplt_epi8(data, control);
        // Combine the results together so we can do a single check
        __m128i Ored  = _mm_or_si128(test_lsquare, test_hyphen);
        __m128i Ored2 = _mm_or_si128(test_langle, test_rangle);
        __m128i Ored3 = _mm_or_si128(test_amp, test_control);
        __m128i Ored4 = _mm_or_si128(Ored, Ored2);
        __m128i Ored5 = _mm_or_si128(Ored3, Ored4);
        int had_it = _mm_movemask_epi8(Ored5);
        if(had_it)
            break;
        // Safe to store as we did the ensure additional above and we only
        // write 1 byte of output per byte of input in this loop.
        _mm_storeu_si128((__m128i_u*)sbdata, data);
        cursor += 16;
        sbdata += 16;
        length -= 16;
        text += 16;
    }
    sb->cursor = cursor;
#endif
#if 1 && !defined(NO_SIMD) && defined(__wasm_simd128__)
    size_t cursor = sb->cursor;
    char* sbdata = sb->data + cursor;
    v128_t lsquare = wasm_i8x16_splat('[');
    v128_t hyphen  = wasm_i8x16_splat('-');
    v128_t langle  = wasm_i8x16_splat('<');
    v128_t rangle  = wasm_i8x16_splat('>');
    v128_t amp     = wasm_i8x16_splat('&');
    v128_t control = wasm_i8x16_splat(32);
    while(length >= 16){
        // This code is straightforward. Check each 16byte chunk for the
        // presence of one of the special characters.  Also check for control
        // characters (ascii < 32), as those are not valid in html, with the
        // exception of newline.
        //
        // For the common case of no special character this is much faster
        // than the byte at a time processing we'd otherwise have to do.
        v128_t data         = wasm_v128_load(text);
        v128_t test_lsquare = wasm_i8x16_eq(data, lsquare);
        v128_t test_hyphen  = wasm_i8x16_eq(data, hyphen);
        v128_t test_langle  = wasm_i8x16_eq(data, langle);
        v128_t test_rangle  = wasm_i8x16_eq(data, rangle);
        v128_t test_amp     = wasm_i8x16_eq(data, amp);
        v128_t test_control = wasm_i8x16_eq(data, control);
        // Combine the results together so we can do a single check
        v128_t Ored  = test_lsquare | test_hyphen;
        v128_t Ored2 = test_langle | test_rangle;
        v128_t Ored3 = test_amp | test_control;
        v128_t Ored4 = Ored | Ored2;
        v128_t Ored5 = Ored3 | Ored4;
        int had_it = wasm_i8x16_bitmask(Ored5);
        if(had_it)
            break;
        // Safe to store as we did the ensure additional above and we only
        // write 1 byte of output per byte of input in this loop.
        wasm_v128_store(sbdata, data);
        cursor += 16;
        sbdata += 16;
        length -= 16;
        text += 16;
    }
    sb->cursor = cursor;
#endif
#if 1 && !defined(NO_SIMD) && defined(__ARM_NEON)
    size_t cursor = sb->cursor;
    unsigned char* sbdata = (unsigned char*)sb->data + cursor;
    uint8x16_t lsquare = vdupq_n_u8('[');
    uint8x16_t hyphen  = vdupq_n_u8('-');
    uint8x16_t langle  = vdupq_n_u8('<');
    uint8x16_t rangle  = vdupq_n_u8('>');
    uint8x16_t amp     = vdupq_n_u8('&');
    uint8x16_t control = vdupq_n_u8(32);
    while(length >= 16){
        uint8x16_t data         = vld1q_u8((const unsigned char*)text);
        uint8x16_t test_lsquare = vceqq_u8(data, lsquare);
        uint8x16_t test_hyphen  = vceqq_u8(data, hyphen);
        uint8x16_t test_langle  = vceqq_u8(data, langle);
        uint8x16_t test_rangle  = vceqq_u8(data, rangle);
        uint8x16_t test_amp     = vceqq_u8(data, amp);
        uint8x16_t test_control = vcltq_u8(data, control);
        // Combine the results together so we can do a single
        // check
        uint8x16_t Ored  = vorrq_u8(test_lsquare, test_hyphen);
        uint8x16_t Ored2 = vorrq_u8(test_langle, test_rangle);
        uint8x16_t Ored3 = vorrq_u8(test_amp, test_control);
        uint8x16_t Ored4 = vorrq_u8(Ored, Ored2);
        uint8x16_t Ored5 = vorrq_u8(Ored3, Ored4);
        uint8x8_t shifted = vshrn_n_u16(vreinterpretq_u16_u8(Ored5), 4);
        uint64x1_t had_it = vreinterpret_u64_u8(shifted);

        if(vget_lane_u64(had_it, 0)){
            break;
        }
        // Safe to store as we did the ensure additional above and
        // we only write 1 byte of output per byte of input in
        // this loop.
        vst1q_u8(sbdata, data);
        cursor += 16;
        sbdata += 16;
        length -= 16;
        text += 16;
    }
    sb->cursor = cursor;
#endif
    return write_link_escaped_str_slow(sb, text, length);
}

#if 1 &&!defined(NO_SIMD) && defined(__ARM_NEON)

// leaving this as reference, it is inefficient compared to the shrn trick
#if 0
// Copied from https://stackoverflow.com/a/68694558
static inline
uint32_t
_mm_movemask_aarch64(uint8x16_t input){
    _Alignas(16) const uint8_t ucShift[] = {-7,-6,-5,-4,-3,-2,-1,0,-7,-6,-5,-4,-3,-2,-1,0};
    uint8x16_t vshift = vld1q_u8(ucShift);
    // Mask to only the msb of each lane.
    uint8x16_t vmask = vandq_u8(input, vdupq_n_u8(0x80));

    // Shift the mask into place.
    vmask = vshlq_u8(vmask, vshift);
    uint32_t out = vaddv_u8(vget_low_u8(vmask));
    // combine
    out += vaddv_u8(vget_high_u8(vmask)) << 8;

    return out;
}
#endif

//  shrn trick from
//  https://community.arm.com/arm-community-blogs/b/infrastructure-solutions-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon
// Allows you to achieve a similar effect to _mm_movemask, but you get 4 bits set instead of 1 per 8 bit lane (thus it's a fat mask).
// Usually need to divide by 4 when you count bits or whatever.
force_inline
uint64_t
vector128_to_fatmask(uint8x16_t input){
    uint8x8_t shifted = vshrn_n_u16(vreinterpretq_u16_u8(input), 4);
    uint64_t fatmask = vget_lane_u64(vreinterpret_u64_u8(shifted), 0);
    return fatmask;
}
#endif

static inline
void
analyze_line(ParseLocation* loc){
    if(loc->cursor == loc->line_start)
        return;
    const char* endline = NULL;
    const char* cursor = loc->cursor;
    int nspace = 0;
    size_t length = loc->end - loc->cursor;
#if 1 && !defined(NO_SIMD) && defined(__x86_64__)
    __m128i spaces  = _mm_set1_epi8(' ');
    __m128i cr      = _mm_set1_epi8('\r');
    __m128i tabs    = _mm_set1_epi8('\t');
    while(length >= 16){
        __m128i data         = _mm_loadu_si128((const __m128i*)cursor);
        __m128i test_space = _mm_cmpeq_epi8(data, spaces);
        __m128i test_cr    = _mm_cmpeq_epi8(data, cr);
        __m128i test_tabs  = _mm_cmpeq_epi8(data, tabs);
        __m128i spacecr    = _mm_or_si128(test_space, test_cr);
        __m128i whitespace = _mm_or_si128(spacecr, test_tabs);
        unsigned mask = _mm_movemask_epi8(whitespace);
        int n = ctz_32(~mask);
        nspace += n;
        if(n != 16){
            cursor += n;
            length -= n;
            goto Lafterwhitespace;
        }
        cursor += 16;
        length -= 16;
    }
#endif

#if 1 && !defined(NO_SIMD) && defined(__wasm_simd128__)
    v128_t spaces = wasm_i8x16_splat(' ');
    v128_t cr     = wasm_i8x16_splat('\r');
    v128_t tabs   = wasm_i8x16_splat('\t');
    while(length >= 16){
        v128_t data       = wasm_v128_load(cursor);
        v128_t test_space = wasm_i8x16_eq(data, spaces);
        v128_t test_cr    = wasm_i8x16_eq(data, cr);
        v128_t test_tabs  = wasm_i8x16_eq(data, tabs);
        v128_t whitespace = test_space | test_cr | test_tabs;
        unsigned mask = wasm_i8x16_bitmask(whitespace);
        int n = ctz_32(~mask);
        nspace += n;
        if(n != 16){
            cursor += n;
            length -= n;
            goto Lafterwhitespace;
        }
        cursor += 16;
        length -= 16;
    }
#endif

#if 1 && !defined(NO_SIMD) && defined(__ARM_NEON)
    uint8x16_t spaces = vdupq_n_u8(' ');
    uint8x16_t cr     = vdupq_n_u8('\r');
    uint8x16_t tabs   = vdupq_n_u8('\t');
    while(length >= 16){
        uint8x16_t data       = vld1q_u8((const unsigned char*)cursor);
        uint8x16_t test_space = vceqq_u8(data, spaces);
        uint8x16_t test_cr    = vceqq_u8(data, cr);
        uint8x16_t test_tabs  = vceqq_u8(data, tabs);
        uint8x16_t spacecr    = vorrq_u8(test_space, test_cr);
        uint8x16_t whitespace = vorrq_u8(spacecr, test_tabs);
        uint64_t fatmask = vector128_to_fatmask(whitespace);
        int n = ctz_64(~fatmask)/4;

        nspace += n;
        if(n != 16){
            cursor += n;
            length -= n;
            goto Lafterwhitespace;
        }
        cursor += 16;
        length -= 16;
    }
#endif
    for(;length;length--,cursor++){
        char ch = *cursor;
        switch(ch){
            case ' ': case '\r': case '\t':
                nspace++;
                continue;
            default:
                goto Lafterwhitespace;
        }
    }
    Lafterwhitespace:;
    length = loc->end - cursor;
#if 1 && !defined(NO_SIMD) && defined(__x86_64__)
    __m128i newline = _mm_set1_epi8('\n');
    __m128i zed     = _mm_set1_epi8(0);
    while(length >= 16){
        __m128i data    = _mm_loadu_si128((const __m128i*)(cursor));
        __m128i testnl  = _mm_cmpeq_epi8(data, newline);
        __m128i testzed = _mm_cmpeq_epi8(data, zed);
        __m128i testend = _mm_or_si128(testnl, testzed);
        unsigned end = _mm_movemask_epi8(testend);
        if(end){
            unsigned endoff = ctz_32(end);
            endline = cursor + endoff;
            goto Lfinish;
        }
        cursor += 16;
        length -= 16;
    }
#endif
#if 1 && !defined(NO_SIMD) && defined(__wasm_simd128__)
    v128_t newline = wasm_i8x16_splat('\n');
    v128_t zed     = wasm_i8x16_splat(0);
    while(length >= 16){
        v128_t data    = wasm_v128_load(cursor);
        v128_t testnl  = wasm_i8x16_eq(data, newline);
        v128_t testzed = wasm_i8x16_eq(data, zed);
        v128_t testend = testnl | testzed;
        unsigned end = wasm_i8x16_bitmask(testend);
        if(end){
            unsigned endoff = ctz_32(end);
            endline = cursor + endoff;
            goto Lfinish;
        }
        cursor += 16;
        length -= 16;
    }
#endif
#if 1 && !defined(NO_SIMD) && defined(__ARM_NEON)
    uint8x16_t newline = vdupq_n_u8('\n');
    uint8x16_t zed     = vdupq_n_u8(0);
    while(length >= 16){
        uint8x16_t data    = vld1q_u8((const unsigned char*)cursor);
        uint8x16_t testnl  = vceqq_u8(data, newline);
        uint8x16_t testzed = vceqq_u8(data, zed);
        uint8x16_t testend = vorrq_u8(testnl, testzed);
        uint64_t end       = vector128_to_fatmask(testend);
        if(end){
            unsigned endoff = ctz_64(end)/4;
            endline = cursor + endoff;
            goto Lfinish;
        }
        cursor += 16;
        length -= 16;
    }
#endif
    for(;length;length--,cursor++){
        switch(*cursor){
            case '\n': case '\0':
                endline = cursor;
                goto Lfinish;
            default:
                continue;
        }
    }
    endline = cursor;

    Lfinish:;
    loc->line_end = endline;
    loc->line_start = loc->cursor;
    loc->nspaces = nspace;
}


#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#include "Allocators/allocator.c"
