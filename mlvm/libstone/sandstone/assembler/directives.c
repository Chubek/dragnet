/* Sandstone assembler directives: sections, symbols, data, alignment.
 *
 * Supported directives:
 *   sections  .text .data .rodata .bss | .section NAME
 *   symbols   .globl/.global NAME... | .extern NAME... |
 *             .equ/.set NAME, EXPR
 *   data      .byte/.short/.word/.long/.quad EXPR... |
 *             .ascii/.asciz/.string "..." |
 *             .zero/.space/.skip N[,FILL] | .fill COUNT,SIZE,VALUE |
 *             .align/.balign N[,FILL] | .p2align N[,FILL] | .org OFS[,FILL]
 *   ignored   .type .size (accepted for GAS compatibility)
 *
 * Expressions are integers, defined symbols, parentheses and + - * / %.
 * `.equ` lines are evaluated to a fixpoint (up to 8 rounds) so that
 * constants may refer to symbols defined later in the file.
 */
#include "intern.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- expression evaluation ------------------------------------------ */

typedef struct {
    sandstone_asm_t *ctx;
    const char *p;
    int ok;
} expr_cursor_t;

static int64_t expr_logic(expr_cursor_t *c);

static void expr_ws(expr_cursor_t *c) {
    while (isspace((unsigned char)*c->p))
        c->p++;
}

static int64_t expr_primary(expr_cursor_t *c) {
    expr_ws(c);
    if (*c->p == '(') {
        c->p++;
        int64_t v = expr_logic(c);
        expr_ws(c);
        if (*c->p != ')') {
            c->ok = 0;
            return 0;
        }
        c->p++;
        return v;
    }
    if (*c->p == '-' || *c->p == '+' || *c->p == '~' || *c->p == '!') {
        char op = *c->p++;
        int64_t v = expr_primary(c);
        if (op == '-')
            v = -v;
        else if (op == '~')
            v = ~v;
        else if (op == '!')
            v = !v;
        return v;
    }
    if (isdigit((unsigned char)*c->p)) {
        const char *start = c->p;
        while (isalnum((unsigned char)*c->p) || *c->p == '_' ||
               *c->p == '\'')
            c->p++;
        int64_t v = 0;
        char *tok = sand_strndup(start, (size_t)(c->p - start));
        int ok = tok && sand_parse_integer(tok, &v);
        free(tok);
        if (!ok) {
            c->ok = 0;
            return 0;
        }
        return v;
    }
    if (isalpha((unsigned char)*c->p) || *c->p == '_' || *c->p == '.' ||
        *c->p == '$') {
        const char *start = c->p;
        while (isalnum((unsigned char)*c->p) || *c->p == '_' ||
               *c->p == '.' || *c->p == '$')
            c->p++;
        char *name = sand_strndup(start, (size_t)(c->p - start));
        sand_symbol_t *s = name ? sand_find_symbol(c->ctx, name) : NULL;
        free(name);
        if (!s || !s->defined) {
            c->ok = 0;
            return 0;
        }
        return (int64_t)s->address;
    }
    c->ok = 0;
    return 0;
}

static int64_t expr_mul(expr_cursor_t *c) {
    int64_t v = expr_primary(c);
    for (;;) {
        expr_ws(c);
        if (*c->p == '*') {
            c->p++;
            v *= expr_primary(c);
        } else if (*c->p == '/') {
            c->p++;
            int64_t d = expr_primary(c);
            if (d == 0) {
                c->ok = 0;
                return 0;
            }
            v /= d;
        } else if (*c->p == '%') {
            c->p++;
            int64_t d = expr_primary(c);
            if (d == 0) {
                c->ok = 0;
                return 0;
            }
            v %= d;
        } else {
            return v;
        }
    }
}

static int64_t expr_add(expr_cursor_t *c) {
    int64_t v = expr_mul(c);
    for (;;) {
        expr_ws(c);
        if (*c->p == '+') {
            c->p++;
            v += expr_mul(c);
        } else if (*c->p == '-') {
            c->p++;
            v -= expr_mul(c);
        } else {
            return v;
        }
    }
}

static int64_t expr_shift(expr_cursor_t *c) {
    int64_t v=expr_add(c);
    for(;;){expr_ws(c);if(!strncmp(c->p,"<<",2)){c->p+=2;int64_t n=expr_add(c);if(n<0||n>=64)c->ok=0;else v=(int64_t)((uint64_t)v<<n);}
        else if(!strncmp(c->p,">>",2)){c->p+=2;int64_t n=expr_add(c);if(n<0||n>=64)c->ok=0;else v>>=n;}else return v;}
}
static int64_t expr_bitand(expr_cursor_t*c){int64_t v=expr_shift(c);for(;;){expr_ws(c);if(*c->p=='&'&&c->p[1]!='&'){c->p++;v&=expr_shift(c);}else return v;}}
static int64_t expr_bitxor(expr_cursor_t*c){int64_t v=expr_bitand(c);for(;;){expr_ws(c);if(*c->p=='^'){c->p++;v^=expr_bitand(c);}else return v;}}
static int64_t expr_bitor(expr_cursor_t*c){int64_t v=expr_bitxor(c);for(;;){expr_ws(c);if(*c->p=='|'&&c->p[1]!='|'){c->p++;v|=expr_bitxor(c);}else return v;}}
static int64_t expr_compare(expr_cursor_t*c){int64_t v=expr_bitor(c);for(;;){expr_ws(c);const char*op=c->p;int n=0,kind=0;
    if(!strncmp(op,"==",2)){n=2;kind=1;}else if(!strncmp(op,"!=",2)){n=2;kind=2;}else if(!strncmp(op,"<=",2)){n=2;kind=3;}else if(!strncmp(op,">=",2)){n=2;kind=4;}else if(*op=='<'&&op[1]!='<'){n=1;kind=5;}else if(*op=='>'&&op[1]!='>'){n=1;kind=6;}else return v;
    c->p+=n;int64_t r=expr_bitor(c);switch(kind){case 1:v=v==r;break;case 2:v=v!=r;break;case 3:v=v<=r;break;case 4:v=v>=r;break;case 5:v=v<r;break;default:v=v>r;break;}}}
static int64_t expr_logic_and(expr_cursor_t*c){int64_t v=expr_compare(c);for(;;){expr_ws(c);if(!strncmp(c->p,"&&",2)){c->p+=2;int64_t r=expr_compare(c);v=(v!=0)&&(r!=0);}else return v;}}
static int64_t expr_logic(expr_cursor_t*c){int64_t v=expr_logic_and(c);for(;;){expr_ws(c);if(!strncmp(c->p,"||",2)){c->p+=2;int64_t r=expr_logic_and(c);v=(v!=0)||(r!=0);}else return v;}}

int64_t sand_eval_expr(sandstone_asm_t *ctx, const char *expr, int *ok) {
    expr_cursor_t c;
    c.ctx = ctx;
    c.p = expr ? expr : "";
    c.ok = 1;
    int64_t v = expr_logic(&c);
    expr_ws(&c);
    if (*c.p != '\0')
        c.ok = 0;
    if (ok)
        *ok = c.ok;
    return v;
}

/* ---- directive classification ---------------------------------------- */

static int name_is(const char *mnemonic, const char *name) {
    return strcmp(mnemonic, name) == 0;
}

/* Section-switch directive? On success sets *name_out (static or line
 * operand) and *is_bss_out. */
int sand_directive_section(const sand_line_t *line, const char **name_out,
                           int *is_bss_out) {
    if (line->kind != SAND_LINE_DIRECTIVE)
        return 0;
    const char *m = line->mnemonic;
    if (name_is(m, ".text")) {
        *name_out = ".text";
        *is_bss_out = 0;
        return 1;
    }
    if (name_is(m, ".data")) {
        *name_out = ".data";
        *is_bss_out = 0;
        return 1;
    }
    if (name_is(m, ".rodata")) {
        *name_out = ".rodata";
        *is_bss_out = 0;
        return 1;
    }
    if (name_is(m, ".bss")) {
        *name_out = ".bss";
        *is_bss_out = 1;
        return 1;
    }
    if (name_is(m, ".section") && line->n_operands >= 1 &&
        line->operands[0].sval) {
        /* Strip optional flags (",\"aw\"") and quotes. */
        static char buf[256];
        const char *raw = line->operands[0].sval;
        size_t i = 0;
        if (raw[0] == '"') {
            raw++;
            while (raw[i] && raw[i] != '"' && i < sizeof buf - 1) {
                buf[i] = raw[i];
                i++;
            }
        } else {
            while (raw[i] && raw[i] != ',' && i < sizeof buf - 1) {
                buf[i] = raw[i];
                i++;
            }
            while (i > 0 && isspace((unsigned char)buf[i - 1]))
                i--;
        }
        buf[i] = '\0';
        *name_out = buf;
        *is_bss_out = strcmp(buf, ".bss") == 0;
        return 1;
    }
    return 0;
}

int sand_directive_is_symbol(const sand_line_t *line) {
    if (line->kind != SAND_LINE_DIRECTIVE)
        return 0;
    const char *m = line->mnemonic;
    return name_is(m, ".globl") || name_is(m, ".global") ||
           name_is(m, ".extern") || name_is(m, ".local") ||
           name_is(m, ".equ") || name_is(m, ".set") || name_is(m, ".type") ||
           name_is(m, ".size");
}

/* Apply a symbol directive (pass 1). `.equ`/`.set` may defer to a later
 * fixpoint round when the expression names symbols that are not defined
 * yet; redefinition within one round is an error. */
int sand_directive_symbol(sandstone_asm_t *ctx, const sand_line_t *line,
                          int round) {
    const char *m = line->mnemonic;
    if (name_is(m, ".type") || name_is(m, ".size"))
        return 1; /* accepted, no local effect */
    if (name_is(m, ".globl") || name_is(m, ".global") ||
        name_is(m, ".extern") || name_is(m, ".local")) {
        for (size_t i = 0; i < line->n_operands; i++) {
            const char *name =
                line->operands[i].sval ? line->operands[i].sval : "";
            if (!*name) {
                sand_set_error(ctx, "%s:%u: %s needs a symbol name",
                               line->src_name, line->lineno, m);
                return 0;
            }
            sand_symbol_t *s = sand_find_symbol(ctx, name);
            if (!s) {
                /* create undefined entry */
                sand_symbol_t ns;
                memset(&ns, 0, sizeof ns);
                ns.name = sand_strdup(name);
                ns.section = sand_strdup("");
                if (!ns.name || !ns.section) {
                    free(ns.name);
                    free(ns.section);
                    return 0;
                }
                if (ctx->n_symbols >= SIZE_MAX / sizeof ns)
                    return 0;
                sand_symbol_t *arr = sand_realloc_array(
                    ctx->symbols, ctx->n_symbols + 1, sizeof ns);
                if (!arr)
                    return 0;
                ctx->symbols = arr;
                ctx->symbols[ctx->n_symbols++] = ns;
                s = &ctx->symbols[ctx->n_symbols - 1];
            }
            if (name_is(m, ".globl") || name_is(m, ".global"))
                s->global = 1;
        }
        return 1;
    }
    if (name_is(m, ".equ") || name_is(m, ".set")) {
        if (line->n_operands < 2 || !line->operands[0].sval ||
            !line->operands[1].sval) {
            sand_set_error(ctx, "%s:%u: %s needs NAME, EXPR", line->src_name,
                           line->lineno, m);
            return 0;
        }
        int ok = 0;
        int64_t v = sand_eval_expr(ctx, line->operands[1].sval, &ok);
        if (!ok)
            return 1; /* fixpoint loop retries later */
        sand_symbol_t *s = sand_find_symbol(ctx, line->operands[0].sval);
        if (!s) {
            sand_symbol_t ns;
            memset(&ns, 0, sizeof ns);
            ns.name = sand_strdup(line->operands[0].sval);
            ns.section = sand_strdup("*abs*");
            if (!ns.name || !ns.section) {
                free(ns.name);
                free(ns.section);
                return 0;
            }
            sand_symbol_t *arr = sand_realloc_array(
                ctx->symbols, ctx->n_symbols + 1, sizeof ns);
            if (!arr)
                return 0;
            ctx->symbols = arr;
            ctx->symbols[ctx->n_symbols++] = ns;
            s = &ctx->symbols[ctx->n_symbols - 1];
        }
        if (s->defined && s->round == round &&
            (int64_t)s->address != v && name_is(m, ".equ")) {
            sand_set_error(ctx, "%s:%u: .equ redefines '%s'", line->src_name,
                           line->lineno, s->name);
            return 0;
        }
        s->address = (uint64_t)v;
        s->defined = 1;
        s->round = round;
        free(s->section);
        s->section = sand_strdup("*abs*");
        return s->section != NULL;
    }
    sand_set_error(ctx, "%s:%u: unknown directive '%s'", line->src_name,
                   line->lineno, m);
    return 0;
}

/* ---- data directive sizes (pass 1) ------------------------------------ */

static int operand_count_ok(sandstone_asm_t *ctx, const sand_line_t *line,
                            size_t min) {
    if (line->n_operands < min) {
        sand_set_error(ctx, "%s:%u: %s needs an argument", line->src_name,
                       line->lineno, line->mnemonic);
        return 0;
    }
    return 1;
}

/* Length of a parsed string literal operand (without quotes). */
static int string_length(const char *s, size_t *len_out) {
    if (!s || s[0] != '"')
        return 0;
    size_t n = 0;
    for (s++; *s && *s != '"'; s++) {
        if (*s == '\\' && s[1])
            s++;
        n++;
    }
    if (*s != '"')
        return 0;
    *len_out = n;
    return 1;
}

/* Alignment directive value: .align N is 2^N on GAS/x86, bytes elsewhere.
 * We follow GAS: a small N (<= 16) on a byte-aligned target means 2^N;
 * use .balign for explicit byte counts. */
static uint64_t align_value(const char *mnemonic, int64_t n) {
    if (n < 0)
        return 0;
    if (strcmp(mnemonic, ".p2align") == 0)
        return (uint64_t)n <= 30 ? (1u << (uint64_t)n) : 0;
    if (strcmp(mnemonic, ".balign") == 0)
        return (uint64_t)n;
    /* .align: power-of-two exponent for N <= 16, bytes above. */
    if (n <= 16)
        return 1u << (uint64_t)n;
    return (uint64_t)n;
}

int sand_directive_size(sandstone_asm_t *ctx, const sand_line_t *line,
                        uint32_t *size_out) {
    const char *m = line->mnemonic;
    *size_out = 0;
    if (name_is(m, ".byte"))
        *size_out = (uint32_t)line->n_operands * 1;
    else if (name_is(m, ".short"))
        *size_out = (uint32_t)line->n_operands * 2;
    else if (name_is(m, ".word") || name_is(m, ".long"))
        *size_out = (uint32_t)line->n_operands * 4;
    else if (name_is(m, ".quad"))
        *size_out = (uint32_t)line->n_operands * 8;
    else if (name_is(m, ".ascii") || name_is(m, ".asciz") ||
             name_is(m, ".string")) {
        size_t total = 0;
        for (size_t i = 0; i < line->n_operands; i++) {
            size_t l = 0;
            if (!line->operands[i].sval ||
                !string_length(line->operands[i].sval, &l)) {
                sand_set_error(ctx, "%s:%u: %s needs quoted strings",
                               line->src_name, line->lineno, m);
                return 0;
            }
            total += l;
        }
        if (!name_is(m, ".ascii"))
            total += 1;
        *size_out = (uint32_t)total;
        return 1;
    } else if (name_is(m, ".zero") || name_is(m, ".space") ||
               name_is(m, ".skip")) {
        if (!operand_count_ok(ctx, line, 1))
            return 0;
        int ok = 0;
        int64_t n = sand_eval_expr(
            ctx, line->operands[0].sval ? line->operands[0].sval : "", &ok);
        if (!ok || n < 0) {
            sand_set_error(ctx, "%s:%u: %s needs a constant count",
                           line->src_name, line->lineno, m);
            return 0;
        }
        *size_out = (uint32_t)n;
        return 1;
    } else if (name_is(m, ".fill")) {
        if (line->n_operands < 3) {
            sand_set_error(ctx, "%s:%u: .fill needs COUNT,SIZE,VALUE",
                           line->src_name, line->lineno);
            return 0;
        }
        int ok = 0;
        int64_t count = sand_eval_expr(ctx, line->operands[0].sval, &ok);
        int ok2 = 0;
        int64_t size =
            sand_eval_expr(ctx, line->operands[1].sval, &ok2);
        if (!ok || !ok2 || count < 0 || size < 0) {
            sand_set_error(ctx, "%s:%u: .fill needs constant COUNT,SIZE",
                           line->src_name, line->lineno);
            return 0;
        }
        *size_out = (uint32_t)(count * size);
        return 1;
    } else if (name_is(m, ".align") || name_is(m, ".balign") ||
               name_is(m, ".p2align") || name_is(m, ".org")) {
        /* Size depends on the address; the binarizer pads directly and
         * assigns size 0 here (padding accounted at layout time). */
        *size_out = 0;
        return 1;
    } else {
        sand_set_error(ctx, "%s:%u: unknown directive '%s'", line->src_name,
                       line->lineno, m);
        return 0;
    }
    if (line->kind == SAND_LINE_DIRECTIVE && line->n_operands == 0 &&
        !name_is(m, ".ascii") && !name_is(m, ".asciz") &&
        !name_is(m, ".string")) {
        sand_set_error(ctx, "%s:%u: %s needs an argument", line->src_name,
                       line->lineno, m);
        return 0;
    }
    return 1;
}

/* ---- data directive emission (pass 2) ---------------------------------- */

static void put_u(sand_section_t *sec, uint64_t v, unsigned n, int be) {
    if (sec->len + n > sec->cap) {
        size_t ncap = sec->cap ? sec->cap * 2 : 64;
        while (ncap < sec->len + n)
            ncap *= 2;
        sec->bytes = realloc(sec->bytes, ncap);
        sec->cap = ncap;
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned sh = be ? (n - 1 - i) * 8 : i * 8;
        sec->bytes[sec->len++] = (uint8_t)((v >> sh) & 0xFF);
    }
}

/* Append a relocation for a directive-emitted absolute field. */
static int add_abs_reloc(sandstone_asm_t *ctx, uint32_t sec_idx,
                         uint32_t offset, const char *symbol, int64_t addend,
                         uint32_t width_bits) {
    sand_reloc_intern_t *arr = sand_realloc_array(ctx->relocs,
                                                 ctx->n_relocs + 1,
                                                 sizeof *arr);
    if (!arr)
        return 0;
    ctx->relocs = arr;
    sand_reloc_intern_t *r = &ctx->relocs[ctx->n_relocs++];
    memset(r, 0, sizeof *r);
    r->sec_idx = sec_idx;
    r->offset = offset;
    r->symbol = sand_strdup(symbol);
    r->addend = addend;
    r->type = sand_strdup("ABS");
    r->width_bits = width_bits;
    r->pcrel = 0;
    return r->symbol && r->type;
}

/* Emit one EXPR of `width` bytes; symbols become relocations/addends. */
static int emit_expr(sandstone_asm_t *ctx, const sand_line_t *line,
                     const char *expr, unsigned width, uint32_t sec_idx) {
    /* Fast path: plain symbol name -> absolute relocation. */
    sand_section_t *sec = &ctx->sections[sec_idx];
    if (sand_is_name(expr)) {
        sand_symbol_t *s = sand_find_symbol(ctx, expr);
        if (s && s->defined) {
            put_u(sec, s->address, width, ctx->byte_order);
            return 1;
        }
        uint32_t at = (uint32_t)sec->len;
        put_u(sec, 0, width, ctx->byte_order);
        if (!add_abs_reloc(ctx, sec_idx, at, expr, 0, width * 8))
            return 0;
        return 1;
    }
    /* General expression: evaluate; on undefined symbols keep a reloc
     * when the expression is exactly `symbol + constant`. */
    int ok = 0;
    int64_t v = sand_eval_expr(ctx, expr, &ok);
    if (ok) {
        put_u(sec, (uint64_t)v, width, ctx->byte_order);
        return 1;
    }
    /* symbol +/- constant? */
    char *plus = strrchr((char *)expr, '+');
    char *minus = strrchr((char *)expr, '-');
    char *sep = NULL;
    if (plus && (!minus || plus > minus))
        sep = plus;
    else if (minus)
        sep = minus;
    if (sep && sep != expr) {
        char *name = sand_strndup(expr, (size_t)(sep - expr));
        char *trimmed = name ? sand_trim(name) : NULL;
        int64_t add = 0;
        int ok2 = trimmed && sand_is_name(trimmed) &&
                  sand_parse_integer(sep + (sep == plus ? 1 : 0), &add);
        if (sep == minus && ok2)
            add = -add;
        if (ok2) {
            sand_symbol_t *s = sand_find_symbol(ctx, trimmed);
            if (s && s->defined) {
                put_u(sec, (uint64_t)((int64_t)s->address + add), width,
                      ctx->byte_order);
                free(name);
                return 1;
            }
            uint32_t at = (uint32_t)sec->len;
            put_u(sec, 0, width, ctx->byte_order);
            int rc = add_abs_reloc(ctx, sec_idx, at, trimmed, add,
                                   width * 8);
            free(name);
            return rc;
        }
        free(name);
    }
    sand_set_error(ctx, "%s:%u: cannot evaluate '%s'", line->src_name,
                   line->lineno, expr);
    return 0;
}

static int emit_string(sand_section_t *sec, const char *s) {
    if (!s || s[0] != '"')
        return 0;
    for (s++; *s && *s != '"'; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            default: c = (uint8_t)*s; break;
            }
        }
        if (sec->len + 1 > sec->cap) {
            size_t ncap = sec->cap ? sec->cap * 2 : 64;
            uint8_t *nb = realloc(sec->bytes, ncap);
            if (!nb)
                return 0;
            sec->bytes = nb;
            sec->cap = ncap;
        }
        sec->bytes[sec->len++] = c;
    }
    return *s == '"';
}

int sand_emit_directive(sandstone_asm_t *ctx, const sand_line_t *line,
                        uint32_t sec_idx, uint64_t address) {
    (void)address;
    const char *m = line->mnemonic;
    sand_section_t *sec = &ctx->sections[sec_idx];
    unsigned width = 0;
    if (name_is(m, ".byte"))
        width = 1;
    else if (name_is(m, ".short"))
        width = 2;
    else if (name_is(m, ".word") || name_is(m, ".long"))
        width = 4;
    else if (name_is(m, ".quad"))
        width = 8;
    if (width) {
        for (size_t i = 0; i < line->n_operands; i++) {
            if (!emit_expr(ctx, line,
                           line->operands[i].sval ? line->operands[i].sval
                                                  : "",
                           width, sec_idx))
                return 0;
        }
        return 1;
    }
    if (name_is(m, ".ascii") || name_is(m, ".asciz") ||
        name_is(m, ".string")) {
        for (size_t i = 0; i < line->n_operands; i++) {
            if (!emit_string(sec, line->operands[i].sval)) {
                sand_set_error(ctx, "%s:%u: %s needs quoted strings",
                               line->src_name, line->lineno, m);
                return 0;
            }
        }
        if (!name_is(m, ".ascii"))
            put_u(sec, 0, 1, 0);
        return 1;
    }
    if (name_is(m, ".zero") || name_is(m, ".space") ||
        name_is(m, ".skip")) {
        int ok = 0;
        int64_t n = sand_eval_expr(ctx, line->operands[0].sval, &ok);
        int64_t fill = 0;
        if (line->n_operands > 1) {
            int ok2 = 0;
            fill = sand_eval_expr(ctx, line->operands[1].sval, &ok2);
            if (!ok2)
                ok = 0;
        }
        if (!ok || n < 0) {
            sand_set_error(ctx, "%s:%u: %s needs a constant count",
                           line->src_name, line->lineno, m);
            return 0;
        }
        for (int64_t i = 0; i < n; i++)
            put_u(sec, (uint64_t)fill, 1, 0);
        return 1;
    }
    if (name_is(m, ".fill")) {
        int ok = 0, ok2 = 0, ok3 = 0;
        int64_t count = sand_eval_expr(ctx, line->operands[0].sval, &ok);
        int64_t size = sand_eval_expr(ctx, line->operands[1].sval, &ok2);
        int64_t value = sand_eval_expr(ctx, line->operands[2].sval, &ok3);
        if (!ok || !ok2 || !ok3 || count < 0 || size < 0 || size > 8) {
            sand_set_error(ctx, "%s:%u: invalid .fill arguments",
                           line->src_name, line->lineno);
            return 0;
        }
        for (int64_t i = 0; i < count; i++)
            put_u(sec, (uint64_t)value, (unsigned)size, ctx->byte_order);
        return 1;
    }
    sand_set_error(ctx, "%s:%u: cannot emit '%s' here", line->src_name,
                   line->lineno, m);
    return 0;
}

/* Alignment/origin padding. With emit=0 only the pad length is computed
 * (added to the line size by the caller via *pad_out semantics here:
 * returns the pad length, or -1 on error). */
int sand_directive_pad(sandstone_asm_t *ctx, const sand_line_t *line,
                       uint32_t sec_idx, uint64_t address, int emit) {
    const char *m = line->mnemonic;
    int ok = 0;
    int64_t arg = line->n_operands >= 1
                      ? sand_eval_expr(ctx, line->operands[0].sval, &ok)
                      : 0;
    if (line->n_operands < 1 || !ok) {
        sand_set_error(ctx, "%s:%u: %s needs a constant argument",
                       line->src_name, line->lineno, m);
        return -1;
    }
    int64_t fill = 0;
    if (line->n_operands > 1) {
        int ok2 = 0;
        fill = sand_eval_expr(ctx, line->operands[1].sval, &ok2);
        if (!ok2) {
            sand_set_error(ctx, "%s:%u: %s needs a constant fill",
                           line->src_name, line->lineno, m);
            return -1;
        }
    }
    uint64_t want = 0;
    if (name_is(m, ".org")) {
        if (arg < 0 || (uint64_t)arg < address) {
            sand_set_error(ctx, "%s:%u: .org moves backwards",
                           line->src_name, line->lineno);
            return -1;
        }
        want = (uint64_t)arg - address;
    } else {
        uint64_t align = align_value(m, arg);
        if (align == 0) {
            sand_set_error(ctx, "%s:%u: invalid alignment", line->src_name,
                           line->lineno);
            return -1;
        }
        uint64_t mis = address % align;
        want = mis ? align - mis : 0;
    }
    if (want > 0x1000000) {
        sand_set_error(ctx, "%s:%u: padding exceeds 16 MiB", line->src_name,
                       line->lineno);
        return -1;
    }
    if (emit && want) {
        sand_section_t *sec = &ctx->sections[sec_idx];
        uint8_t fb = (uint8_t)(fill & 0xFF);
        for (uint64_t i = 0; i < want; i++)
            put_u(sec, fb, 1, 0);
    }
    return (int)want;
}
