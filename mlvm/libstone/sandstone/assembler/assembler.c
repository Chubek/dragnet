/* Sandstone assembler context and public C API. */
#include "intern.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *sand_strdup(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (out)
        memcpy(out, s, n);
    return out;
}

char *sand_strndup(const char *s, size_t n) {
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void *sand_realloc_array(void *ptr, size_t n, size_t elem) {
    if (n > 0 && elem > SIZE_MAX / n)
        return NULL;
    return realloc(ptr, n * elem);
}

void sand_set_error(sandstone_asm_t *ctx, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof ctx->error, fmt, ap);
    va_end(ap);
}

sand_symbol_t *sand_find_symbol(sandstone_asm_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->n_symbols; i++)
        if (strcmp(ctx->symbols[i].name, name) == 0)
            return &ctx->symbols[i];
    return NULL;
}

sand_section_t *sand_find_section(sandstone_asm_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->n_sections; i++)
        if (strcmp(ctx->sections[i].name, name) == 0)
            return &ctx->sections[i];
    return NULL;
}

sand_section_t *sand_require_section(sandstone_asm_t *ctx,
                                     const char *name) {
    sand_section_t *s = sand_find_section(ctx, name);
    if (s)
        return s;
    sand_section_t *arr = sand_realloc_array(ctx->sections,
                                            ctx->n_sections + 1,
                                            sizeof *arr);
    if (!arr)
        return NULL;
    ctx->sections = arr;
    s = &ctx->sections[ctx->n_sections++];
    memset(s, 0, sizeof *s);
    s->name = sand_strdup(name);
    s->align = 1;
    if (!s->name) {
        ctx->n_sections--;
        return NULL;
    }
    return s;
}

/* ---- syntax selection ---- */

static int load_syntax(sandstone_asm_t *ctx, const char *name) {
    const DomSymAs_Syntax *syn = NULL;
    if (name && *name) {
        syn = domsymas_find_syntax(ctx->bank, name);
        if (!syn) {
            snprintf(ctx->error, sizeof ctx->error,
                     "unknown syntax '%s'", name);
            return 0;
        }
    } else if (domsymas_syntax_count(ctx->bank) > 0) {
        syn = domsymas_syntax_at(ctx->bank, 0);
    } else {
        /* No syntax records: neutral defaults. */
        free(ctx->comment);
        free(ctx->reg_prefix);
        free(ctx->imm_prefix);
        free(ctx->separator);
        ctx->comment = sand_strdup("#");
        ctx->reg_prefix = sand_strdup("");
        ctx->imm_prefix = sand_strdup("");
        ctx->separator = sand_strdup(",");
        ctx->operand_order = 0;
        free(ctx->syntax_name);
        ctx->syntax_name = sand_strdup("");
        return ctx->comment && ctx->separator && ctx->syntax_name;
    }
    free(ctx->comment);
    free(ctx->reg_prefix);
    free(ctx->imm_prefix);
    free(ctx->separator);
    free(ctx->syntax_name);
    ctx->comment = sand_strdup(syn->comment ? syn->comment : "#");
    ctx->reg_prefix = sand_strdup(syn->reg_prefix ? syn->reg_prefix : "");
    ctx->imm_prefix = sand_strdup(syn->imm_prefix ? syn->imm_prefix : "");
    ctx->separator = sand_strdup(syn->separator ? syn->separator : ",");
    ctx->syntax_name = sand_strdup(syn->name ? syn->name : "");
    ctx->operand_order = syn->operand_order;
    /* Find the numeric syntax id for disassembly-adjacent uses. */
    ctx->syntax_id = 0;
    for (uint32_t i = 0; i < domsymas_syntax_count(ctx->bank); i++) {
        const DomSymAs_Syntax *s = domsymas_syntax_at(ctx->bank, i);
        if (s && syn->name && s->name && strcmp(s->name, syn->name) == 0)
            ctx->syntax_id = i;
    }
    return ctx->comment && ctx->reg_prefix && ctx->imm_prefix &&
           ctx->separator && ctx->syntax_name;
}

static int load_defaults(sandstone_asm_t *ctx) {
    /* Default syntax: first record. */
    if (!load_syntax(ctx, NULL))
        return 0;
    /* Default features: the first target's feature set. */
    free(ctx->enabled);
    ctx->enabled = NULL;
    ctx->n_enabled = 0;
    if (domsymas_target_count(ctx->bank) > 0) {
        const DomSymAs_Target *t = domsymas_target_at(ctx->bank, 0);
        if (t) {
            ctx->byte_order = (int)t->byte_order;
            if (t->feature_count) {
                ctx->enabled = malloc(t->feature_count * sizeof *ctx->enabled);
                if (!ctx->enabled)
                    return 0;
                memcpy(ctx->enabled, t->features,
                       t->feature_count * sizeof *ctx->enabled);
                ctx->n_enabled = t->feature_count;
            }
        }
    }
    return 1;
}

static sandstone_asm_t *asm_alloc(void) {
    sandstone_asm_t *ctx = calloc(1, sizeof *ctx);
    if (!ctx)
        return NULL;
    snprintf(ctx->error, sizeof ctx->error, "ok");
    return ctx;
}

int sandstone_asm_new(sandstone_asm_t **out, const char *bank_path,
                      char *err, size_t err_n) {
    sandstone_asm_t *ctx = asm_alloc();
    if (!ctx)
        return -1;
    if (domsymas_open_file(bank_path, &ctx->bank) != DOMSYMAS_OK) {
        snprintf(ctx->error, sizeof ctx->error, "cannot open bank '%s'",
                 bank_path ? bank_path : "(null)");
        if (err && err_n) {
            snprintf(err, err_n, "%s", ctx->error);
        }
        free(ctx);
        return -1;
    }
    if (!load_defaults(ctx)) {
        if (err && err_n)
            snprintf(err, err_n, "%s", ctx->error);
        domsymas_close(ctx->bank);
        free(ctx);
        return -1;
    }
    *out = ctx;
    return 0;
}

int sandstone_asm_new_from_memory(sandstone_asm_t **out,
                                  const uint8_t *data, size_t len, char *err,
                                  size_t err_n) {
    sandstone_asm_t *ctx = asm_alloc();
    if (!ctx)
        return -1;
    if (domsymas_open_memory(data, len, &ctx->bank) != DOMSYMAS_OK) {
        snprintf(ctx->error, sizeof ctx->error, "cannot open bank image");
        if (err && err_n)
            snprintf(err, err_n, "%s", ctx->error);
        free(ctx);
        return -1;
    }
    if (!load_defaults(ctx)) {
        if (err && err_n)
            snprintf(err, err_n, "%s", ctx->error);
        domsymas_close(ctx->bank);
        free(ctx);
        return -1;
    }
    *out = ctx;
    return 0;
}

static void free_results(sandstone_asm_t *ctx) {
    for (size_t i = 0; i < ctx->n_sections; i++) {
        free(ctx->sections[i].name);
        free(ctx->sections[i].bytes);
    }
    free(ctx->sections);
    ctx->sections = NULL;
    ctx->n_sections = 0;
    for (size_t i = 0; i < ctx->n_symbols; i++) {
        free(ctx->symbols[i].name);
        free(ctx->symbols[i].section);
    }
    free(ctx->symbols);
    ctx->symbols = NULL;
    ctx->n_symbols = 0;
    for (size_t i = 0; i < ctx->n_relocs; i++) {
        free(ctx->relocs[i].symbol);
        free(ctx->relocs[i].type);
    }
    free(ctx->relocs);
    ctx->relocs = NULL;
    ctx->n_relocs = 0;
    sand_free_lines(ctx);
    free(ctx->image);
    ctx->image = NULL;
    ctx->image_len = 0;
    ctx->image_valid = 0;
    free(ctx->pub_relocs);
    ctx->pub_relocs = NULL;
    free(ctx->pub_syms);
    ctx->pub_syms = NULL;
}

void sandstone_asm_free(sandstone_asm_t *ctx) {
    if (!ctx)
        return;
    sand_lua_reset(ctx);
    free_results(ctx);
    if (ctx->bank)
        domsymas_close(ctx->bank);
    free(ctx->syntax_name);
    free(ctx->comment);
    free(ctx->reg_prefix);
    free(ctx->imm_prefix);
    free(ctx->separator);
    free(ctx->enabled);
    free(ctx->hook_path);
    free(ctx);
}

int sandstone_asm_set_syntax(sandstone_asm_t *ctx, const char *name) {
    if (!name || !*name)
        return 0;
    return load_syntax(ctx, name) ? 0 : -1;
}

static int has_feature(sandstone_asm_t *ctx, uint32_t id) {
    for (size_t i = 0; i < ctx->n_enabled; i++)
        if (ctx->enabled[i] == id)
            return 1;
    return 0;
}

int sandstone_asm_enable_feature(sandstone_asm_t *ctx, const char *name) {
    const DomSymAs_Feature *f = domsymas_find_feature(ctx->bank, name);
    if (!f) {
        snprintf(ctx->error, sizeof ctx->error, "unknown feature '%s'",
                 name);
        return -1;
    }
    /* Feature ids equal the record index; recover it by scan. */
    for (uint32_t i = 0; i < domsymas_feature_count(ctx->bank); i++) {
        const DomSymAs_Feature *c = domsymas_feature_at(ctx->bank, i);
        if (c && strcmp(c->name, f->name) == 0) {
            if (has_feature(ctx, i))
                return 0;
            uint32_t *narr =
                realloc(ctx->enabled, (ctx->n_enabled + 1) * sizeof *narr);
            if (!narr)
                return -1;
            ctx->enabled = narr;
            ctx->enabled[ctx->n_enabled++] = i;
            return 0;
        }
    }
    return -1;
}

int sandstone_asm_disable_feature(sandstone_asm_t *ctx, const char *name) {
    const DomSymAs_Feature *f = domsymas_find_feature(ctx->bank, name);
    if (!f) {
        snprintf(ctx->error, sizeof ctx->error, "unknown feature '%s'",
                 name);
        return -1;
    }
    for (uint32_t i = 0; i < domsymas_feature_count(ctx->bank); i++) {
        const DomSymAs_Feature *c = domsymas_feature_at(ctx->bank, i);
        if (c && strcmp(c->name, f->name) == 0) {
            for (size_t k = 0; k < ctx->n_enabled; k++) {
                if (ctx->enabled[k] == i) {
                    ctx->enabled[k] = ctx->enabled[--ctx->n_enabled];
                    return 0;
                }
            }
            return 0;
        }
    }
    return -1;
}

int sandstone_asm_assemble_string(sandstone_asm_t *ctx, const char *src,
                                  const char *src_name) {
    sand_lua_reset(ctx);
    free_results(ctx);
    char *processed = sand_run_hook(ctx, src ? src : "",
                                    src_name ? src_name : "<string>");
    if (!processed) {
        sand_set_error(ctx, "preprocessing failed");
        return -1;
    }
    char *expanded = sand_preprocess_source(ctx, processed,
                                           src_name ? src_name : "<string>");
    free(processed);
    if (!expanded)
        return -1;
    int ok = sand_parse_source(ctx, expanded,
                               src_name ? src_name : "<string>");
    free(expanded);
    if (!ok)
        return -1;
    if (!sand_assemble_lines(ctx))
        return -1;
    snprintf(ctx->error, sizeof ctx->error, "ok");
    return 0;
}

int sandstone_asm_assemble_file(sandstone_asm_t *ctx, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        sand_set_error(ctx, "cannot open '%s'", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        sand_set_error(ctx, "cannot read '%s'", path);
        return -1;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = n ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        sand_set_error(ctx, "cannot read '%s'", path);
        return -1;
    }
    buf[n] = '\0';
    int rc = sandstone_asm_assemble_string(ctx, buf, path);
    free(buf);
    return rc;
}

uint32_t sandstone_asm_section_count(const sandstone_asm_t *ctx) {
    return (uint32_t)ctx->n_sections;
}

const char *sandstone_asm_section_name(const sandstone_asm_t *ctx,
                                       uint32_t i) {
    if (i >= ctx->n_sections)
        return NULL;
    return ctx->sections[i].name;
}

const uint8_t *sandstone_asm_section_bytes(const sandstone_asm_t *ctx,
                                           uint32_t i, size_t *len_out) {
    if (i >= ctx->n_sections)
        return NULL;
    if (len_out)
        *len_out = ctx->sections[i].len;
    return ctx->sections[i].bytes;
}

uint64_t sandstone_asm_section_base(const sandstone_asm_t *ctx,
                                    uint32_t i) {
    if (i >= ctx->n_sections)
        return 0;
    return ctx->sections[i].base;
}

/* Flat image: sections concatenated at their bases (BSS zero-filled). */
static int rebuild_image(sandstone_asm_t *ctx) {
    if (ctx->image_valid)
        return 1;
    uint64_t end = 0;
    for (size_t i = 0; i < ctx->n_sections; i++) {
        uint64_t e = ctx->sections[i].base + ctx->sections[i].len;
        if (e > end)
            end = e;
    }
    /* BSS sections carry no bytes but occupy image space. */
    free(ctx->image);
    ctx->image = NULL;
    ctx->image_len = 0;
    if (end == 0) {
        ctx->image_valid = 1;
        return 1;
    }
    if (end > 64 * 1024 * 1024)
        return 0;
    ctx->image = calloc((size_t)end, 1);
    if (!ctx->image)
        return 0;
    ctx->image_len = (size_t)end;
    for (size_t i = 0; i < ctx->n_sections; i++) {
        if (ctx->sections[i].len)
            memcpy(ctx->image + ctx->sections[i].base,
                   ctx->sections[i].bytes, ctx->sections[i].len);
    }
    ctx->image_valid = 1;
    return 1;
}

const uint8_t *sandstone_asm_image(const sandstone_asm_t *ctx,
                                   size_t *len_out) {
    if (!rebuild_image((sandstone_asm_t *)ctx))
        return NULL;
    if (len_out)
        *len_out = ctx->image_len;
    return ctx->image;
}

const sandstone_reloc_t *sandstone_asm_relocs(const sandstone_asm_t *ctx,
                                              uint32_t *n_out) {
    free(ctx->pub_relocs);
    ((sandstone_asm_t *)ctx)->pub_relocs = NULL;
    if (ctx->n_relocs) {
        sandstone_reloc_t *arr =
            calloc(ctx->n_relocs, sizeof *arr);
        if (!arr)
            return NULL;
        for (size_t i = 0; i < ctx->n_relocs; i++) {
            arr[i].section = ctx->sections[ctx->relocs[i].sec_idx].name;
            arr[i].offset = ctx->relocs[i].offset;
            arr[i].symbol = ctx->relocs[i].symbol;
            arr[i].addend = ctx->relocs[i].addend;
            arr[i].type = ctx->relocs[i].type;
            arr[i].width_bits = ctx->relocs[i].width_bits;
            arr[i].pcrel = ctx->relocs[i].pcrel;
        }
        ((sandstone_asm_t *)ctx)->pub_relocs = arr;
    }
    if (n_out)
        *n_out = (uint32_t)ctx->n_relocs;
    return ctx->pub_relocs;
}

const sandstone_sym_t *sandstone_asm_symbols(const sandstone_asm_t *ctx,
                                            uint32_t *n_out) {
    free(ctx->pub_syms);
    ((sandstone_asm_t *)ctx)->pub_syms = NULL;
    if (ctx->n_symbols) {
        sandstone_sym_t *arr = calloc(ctx->n_symbols, sizeof *arr);
        if (!arr)
            return NULL;
        for (size_t i = 0; i < ctx->n_symbols; i++) {
            arr[i].name = ctx->symbols[i].name;
            arr[i].section = ctx->symbols[i].section;
            arr[i].address = ctx->symbols[i].address;
            arr[i].global = ctx->symbols[i].global;
            arr[i].defined = ctx->symbols[i].defined;
        }
        ((sandstone_asm_t *)ctx)->pub_syms = arr;
    }
    if (n_out)
        *n_out = (uint32_t)ctx->n_symbols;
    return ctx->pub_syms;
}

/* JSON string escaping for obj output. */
static void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) {
        switch (*s) {
        case '"': fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f); break;
        case '\t': fputs("\\t", f); break;
        case '\r': fputs("\\r", f); break;
        default:
            if ((unsigned char)*s < 0x20)
                fprintf(f, "\\u%04x", *s);
            else
                fputc(*s, f);
        }
    }
    fputc('"', f);
}

int sandstone_asm_write(const sandstone_asm_t *ctx, const char *path,
                        const char *format) {
    int obj = format && strcmp(format, "obj") == 0;
    if (!obj && !(format == NULL || strcmp(format, "bin") == 0)) {
        sand_set_error((sandstone_asm_t *)ctx,
                       "unknown format '%s' (bin or obj)", format);
        return -1;
    }
    if (!obj && ctx->n_relocs > 0) {
        sand_set_error((sandstone_asm_t *)ctx,
                       "flat output with %u unresolved relocation(s); "
                       "use obj format",
                       (unsigned)ctx->n_relocs);
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        sand_set_error((sandstone_asm_t *)ctx, "cannot write '%s'", path);
        return -1;
    }
    int rc = 0;
    if (!obj) {
        size_t len = 0;
        const uint8_t *img = sandstone_asm_image(ctx, &len);
        if (len && (!img || fwrite(img, 1, len, f) != len))
            rc = -1;
    } else {
        fprintf(f, "{\"format\":\"sandstone-obj-1\",\"sections\":[");
        for (size_t i = 0; i < ctx->n_sections; i++) {
            if (i)
                fputs(",", f);
            fprintf(f, "{\"name\":");
            json_string(f, ctx->sections[i].name);
            fprintf(f, ",\"base\":%llu,\"bss\":%s,\"data\":\"",
                    (unsigned long long)ctx->sections[i].base,
                    ctx->sections[i].is_bss ? "true" : "false");
            for (size_t k = 0; k < ctx->sections[i].len; k++)
                fprintf(f, "%02x", ctx->sections[i].bytes[k]);
            fputs("\"}", f);
        }
        fputs("],\"symbols\":[", f);
        for (size_t i = 0; i < ctx->n_symbols; i++) {
            if (i)
                fputs(",", f);
            fprintf(f, "{\"name\":");
            json_string(f, ctx->symbols[i].name);
            fprintf(f, ",\"section\":");
            json_string(f, ctx->symbols[i].section);
            fprintf(f, ",\"address\":%llu,\"global\":%s,\"defined\":%s}",
                    (unsigned long long)ctx->symbols[i].address,
                    ctx->symbols[i].global ? "true" : "false",
                    ctx->symbols[i].defined ? "true" : "false");
        }
        fputs("],\"relocs\":[", f);
        for (size_t i = 0; i < ctx->n_relocs; i++) {
            if (i)
                fputs(",", f);
            fprintf(f, "{\"section\":");
            json_string(f, ctx->sections[ctx->relocs[i].sec_idx].name);
            fprintf(f, ",\"offset\":%u,\"symbol\":",
                    ctx->relocs[i].offset);
            json_string(f, ctx->relocs[i].symbol);
            fprintf(f, ",\"addend\":%lld,\"type\":",
                    (long long)ctx->relocs[i].addend);
            json_string(f, ctx->relocs[i].type);
            fprintf(f, ",\"width\":%u,\"pcrel\":%s}",
                    ctx->relocs[i].width_bits,
                    ctx->relocs[i].pcrel ? "true" : "false");
        }
        fputs("]}\n", f);
    }
    if (fclose(f) != 0)
        rc = -1;
    if (rc != 0)
        sand_set_error((sandstone_asm_t *)ctx, "cannot write '%s'", path);
    return rc;
}

const char *sandstone_asm_error(const sandstone_asm_t *ctx) {
    return ctx ? ctx->error : "null context";
}
