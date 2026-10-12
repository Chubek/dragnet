/* Sandstone source parser: lines -> labels, instructions, directives.
 *
 * Each line has the shape [label:] (directive | mnemonic operands...)
 * [comment]. Comment stripping follows the selected syntax record: the
 * comment token starts a comment at line start, or when preceded by
 * whitespace and followed by whitespace or end of line (so `#4`-style
 * immediates survive a `#` comment character). Operands are split on the
 * syntax separator (whitespace when the separator is blank), aware of
 * `[...]` memory expressions and quoted strings.
 */
#include "intern.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *sand_trim(char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

int sand_is_name(const char *s) {
    if (!s || !*s)
        return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '.'))
        return 0;
    for (s++; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_' || *s == '.' ||
              *s == '$'))
            return 0;
    return 1;
}

int sand_parse_integer(const char *text, int64_t *out) {
    if (!text || !*text)
        return 0;
    while (isspace((unsigned char)*text))
        text++;
    int neg = 0;
    if (*text == '+' || *text == '-') {
        neg = *text == '-';
        text++;
    }
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    } else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        base = 2;
        text += 2;
    }
    if (!*text)
        return 0;
    int64_t v = 0;
    for (; *text; text++) {
        int d = -1;
        if (*text >= '0' && *text <= '9')
            d = *text - '0';
        else if (*text >= 'a' && *text <= 'f')
            d = *text - 'a' + 10;
        else if (*text >= 'A' && *text <= 'F')
            d = *text - 'A' + 10;
        else if (*text == '_' || *text == '\'')
            continue;
        else
            return 0;
        if (d < 0 || d >= base)
            return 0;
        if (v > (INT64_MAX - d) / base)
            return 0;
        v = v * base + d;
    }
    *out = neg ? -v : v;
    return 1;
}

/* Strip the syntax comment, keeping `#4`-style immediates. */
static void strip_comment(sandstone_asm_t *ctx, char *line) {
    const char *tok = ctx->comment && *ctx->comment ? ctx->comment : "#";
    size_t tl = strlen(tok);
    if (!tl)
        return;
    for (char *p = line; (p = strstr(p, tok)) != NULL; p += tl) {
        int at_start = (p == line);
        int pre_ws = at_start || isspace((unsigned char)p[-1]);
        char after = p[tl];
        int post_ws = after == '\0' || isspace((unsigned char)after);
        if ((at_start || pre_ws) && (post_ws || tl > 1)) {
            /* Multi-char tokens (//, ;;) always terminate; single-char
             * tokens only when followed by whitespace/EOL. */
            if (tl > 1 || post_ws) {
                *p = '\0';
                return;
            }
        }
    }
}

static int has_prefix(const char *s, const char *prefix) {
    return prefix && *prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Try to resolve a register name (with optional syntax prefix). */
static int try_register(sandstone_asm_t *ctx, const char *text, int64_t *num) {
    const char *t = text;
    if (has_prefix(t, ctx->reg_prefix))
        t += strlen(ctx->reg_prefix);
    /* AT&T '%', Intel bare, and common '$'-register spellings. */
    const DomSymAs_Register *r = domsymas_find_register(ctx->bank, t);
    if (!r) {
        char *lower = sand_strdup(t);
        for (char *p = lower; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        r = domsymas_find_register(ctx->bank, lower);
        free(lower);
    }
    if (r) {
        *num = (int64_t)r->enc_num;
        return 1;
    }
    return 0;
}

static void classify_operand(sandstone_asm_t *ctx, const char *text,
                             sand_operand_t *op) {
    op->kind = SAND_OP_SYM;
    op->ival = 0;
    op->sval = NULL;
    if (!text || !*text)
        return;
    int64_t v;
    /* Memory expression. */
    if (text[0] == '[') {
        op->kind = SAND_OP_MEM;
        op->sval = sand_strdup(text);
        return;
    }
    /* Immediate prefix ($4 in AT&T). */
    if (has_prefix(text, ctx->imm_prefix)) {
        const char *t = text + strlen(ctx->imm_prefix);
        if (sand_parse_integer(t, &v)) {
            op->kind = SAND_OP_IMM;
            op->ival = v;
            return;
        }
        /* $symbol: a symbolic immediate. */
        op->kind = SAND_OP_SYM;
        op->sval = sand_strdup(t);
        return;
    }
    /* Plain integer. */
    if (sand_parse_integer(text, &v)) {
        op->kind = SAND_OP_IMM;
        op->ival = v;
        return;
    }
    /* Register (with or without the syntax prefix). */
    if (try_register(ctx, text, &v)) {
        op->kind = SAND_OP_REG;
        op->ival = v;
        return;
    }
    /* Otherwise a symbol reference. */
    op->kind = SAND_OP_SYM;
    op->sval = sand_strdup(text);
}

/* Split operand text on the syntax separator, aware of brackets/quotes. */
static char **split_operands(sandstone_asm_t *ctx, char *text, size_t *n_out) {
    size_t cap = 4, n = 0;
    char **out = malloc(cap * sizeof *out);
    const char *sep = ctx->separator;
    int ws_mode = 0;
    char sepc = ',';
    if (!sep || !*sep || (sep[0] && !sep[1] && isspace((unsigned char)sep[0])))
        ws_mode = 1;
    else {
        /* First non-space character of the separator (usually ','). */
        for (const char *p = sep; *p; p++)
            if (!isspace((unsigned char)*p)) {
                sepc = *p;
                break;
            }
    }
    char *start = text;
    int depth = 0;
    int quote = 0;
    for (char *p = text;; p++) {
        char c = *p;
        if (quote) {
            if (c == quote)
                quote = 0;
            else if (c == '\0')
                break;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[' || c == '(') {
            depth++;
        } else if ((c == ']' || c == ')') && depth > 0) {
            depth--;
        } else if (depth == 0 &&
                   (c == '\0' || (!ws_mode && c == sepc) ||
                    (ws_mode && isspace((unsigned char)c)))) {
            char save = c;
            *p = '\0';
            char *t = sand_trim(start);
            if (*t) {
                if (n == cap) {
                    cap *= 2;
                    out = realloc(out, cap * sizeof *out);
                }
                out[n++] = t;
            }
            if (save == '\0')
                break;
            start = p + 1;
            if (ws_mode) {
                while (isspace((unsigned char)*start))
                    start++;
                p = start - 1;
            }
            continue;
        }
        if (c == '\0')
            break;
    }
    *n_out = n;
    return out;
}

static int push_line(sandstone_asm_t *ctx, sand_line_t *line) {
    if (ctx->n_lines == ctx->cap_lines) {
        size_t ncap = ctx->cap_lines ? ctx->cap_lines * 2 : 64;
        sand_line_t *nl =
            realloc(ctx->lines, ncap * sizeof *nl);
        if (!nl)
            return 0;
        ctx->lines = nl;
        ctx->cap_lines = ncap;
    }
    ctx->lines[ctx->n_lines++] = *line;
    return 1;
}

int sand_parse_source(sandstone_asm_t *ctx, const char *src,
                      const char *src_name) {
    char *buf = sand_strdup(src ? src : "");
    if (!buf)
        return 0;
    unsigned lineno = 0;
    int failed = 0;
    for (char *save = NULL, *raw = strtok_r(buf, "\n", &save); raw;
         raw = strtok_r(NULL, "\n", &save)) {
        lineno++;
        /* Keep CR-free working copy. */
        size_t rl = strlen(raw);
        if (rl && raw[rl - 1] == '\r')
            raw[--rl] = '\0';
        char *line = sand_strdup(raw);
        if (!line) {
            failed = 1;
            break;
        }
        strip_comment(ctx, line);
        char *t = sand_trim(line);
        if (!*t) {
            free(line);
            continue;
        }
        sand_line_t ln;
        memset(&ln, 0, sizeof ln);
        ln.raw = sand_strdup(raw);
        ln.lineno = lineno;
        ln.src_name = src_name;
        ln.kind = SAND_LINE_EMPTY;

        /* Label prefix (`name:`), possibly followed by more text. */
        char *colon = NULL;
        {
            /* A label colon must precede any whitespace-separated token
             * boundary issues: scan the first token for ':'. */
            char *p = t;
            while (*p && !isspace((unsigned char)*p)) {
                if (*p == ':') {
                    colon = p;
                    break;
                }
                p++;
            }
        }
        if (colon) {
            *colon = '\0';
            char *name = sand_trim(t);
            if (!sand_is_name(name)) {
                sand_set_error(ctx, "%s:%u: invalid label '%s'", src_name,
                               lineno, name);
                failed = 1;
                free(ln.raw);
                free(line);
                break;
            }
            ln.label = sand_strdup(name);
            t = sand_trim(colon + 1);
        }
        if (*t) {
            /* Mnemonic / directive token. */
            char *end = t;
            while (*end && !isspace((unsigned char)*end))
                end++;
            char save = *end;
            *end = '\0';
            ln.mnemonic = sand_strdup(t);
            char *rest = save ? sand_trim(end + 1) : end;
            if (ln.mnemonic[0] == '.') {
                ln.kind = SAND_LINE_DIRECTIVE;
            } else {
                ln.kind = SAND_LINE_INSN;
            }
            size_t n_ops = 0;
            char **ops = NULL;
            if (*rest)
                ops = split_operands(ctx, rest, &n_ops);
            /* AT&T operand order is reversed relative to encoding order. */
            if (ln.kind == SAND_LINE_INSN && ctx->operand_order == 1 &&
                n_ops > 1) {
                for (size_t i = 0; i < n_ops / 2; i++) {
                    char *tmp = ops[i];
                    ops[i] = ops[n_ops - 1 - i];
                    ops[n_ops - 1 - i] = tmp;
                }
            }
            if (n_ops) {
                ln.operands = calloc(n_ops, sizeof *ln.operands);
                if (!ln.operands) {
                    failed = 1;
                    free(ops);
                    free(ln.mnemonic);
                    free(ln.label);
                    free(ln.raw);
                    free(line);
                    break;
                }
                ln.n_operands = n_ops;
                for (size_t i = 0; i < n_ops; i++) {
                    if (ln.kind == SAND_LINE_DIRECTIVE) {
                        /* Directives keep raw operand strings. */
                        ln.operands[i].kind = SAND_OP_SYM;
                        ln.operands[i].sval = sand_strdup(ops[i]);
                    } else {
                        classify_operand(ctx, ops[i], &ln.operands[i]);
                    }
                }
                free(ops);
            }
        } else if (!ln.label) {
            free(ln.raw);
            free(line);
            continue;
        }
        if (!push_line(ctx, &ln)) {
            failed = 1;
            free(line);
            break;
        }
        free(line);
    }
    free(buf);
    return !failed;
}

void sand_free_lines(sandstone_asm_t *ctx) {
    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        free(ln->label);
        free(ln->mnemonic);
        free(ln->raw);
        for (size_t k = 0; k < ln->n_operands; k++)
            free(ln->operands[k].sval);
        free(ln->operands);
    }
    free(ctx->lines);
    ctx->lines = NULL;
    ctx->n_lines = ctx->cap_lines = 0;
}
