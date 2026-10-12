/* Sandstone binarizer: two-pass assembly over parsed lines.
 *
 * Pass 1 assigns every line a section, an address and a size. Instructions
 * are trial-encoded with symbol operands passed by name, which yields both
 * the selected form and the instruction size; labels capture the current
 * address. `.equ` constants are evaluated to a fixpoint.
 *
 * Pass 2 re-encodes, appends bytes to each section, and resolves
 * relocations against locally defined labels (absolute: S + A;
 * PC-relative: S + A - P, where P is the end of the containing
 * instruction when the relocated field is a strict suffix of the
 * instruction and the start of the instruction otherwise). Relocations
 * against undefined symbols are kept for `obj` output and rejected for
 * flat `bin` output.
 */
#include "intern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* x86 condition codes, used when a COND operand names a condition. */
static int cond_code(const char *name, int64_t *out) {
    static const struct { const char *name; int64_t code; } tab[] = {
        {"o", 0}, {"no", 1}, {"b", 2}, {"c", 2}, {"ae", 3}, {"nc", 3},
        {"e", 4}, {"z", 4}, {"ne", 5}, {"nz", 5}, {"be", 6}, {"na", 6},
        {"a", 7}, {"nbe", 7}, {"s", 8}, {"ns", 9}, {"p", 10}, {"pe", 10},
        {"np", 11}, {"po", 11}, {"l", 12}, {"nge", 12}, {"ge", 13},
        {"nl", 13}, {"le", 14}, {"ng", 14}, {"g", 15}, {"nle", 15},
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(name, tab[i].name) == 0) {
            *out = tab[i].code;
            return 1;
        }
    return 0;
}

static const char *const kPosNames[][4] = {
    {"dst", "rd", "r0", "a"},
    {"src", "rs1", "r1", "b"},
    {"rs2", "r2", "c"},
    {"rs3", "r3", "d"},
};

/* Operand-kind compatibility between a parsed operand and a form's
 * descriptor kind (DOMSYMAS_OP_*). */
static int kind_matches(const sand_operand_t *op, uint32_t desc) {
    switch (op->kind) {
    case SAND_OP_REG: return desc == 0; /* REG */
    case SAND_OP_IMM:
        return desc == 1 || desc == 2 || desc == 5 || desc == 4 ||
               desc == 7; /* IMM DISP REL ABS COND */
    case SAND_OP_SYM:
        return desc == 6 || desc == 5 || desc == 4 || desc == 2 ||
               desc == 1 || desc == 7; /* SYM REL ABS DISP IMM COND */
    case SAND_OP_MEM: return desc == 3; /* MEM */
    case SAND_OP_COND: return desc == 7; /* COND */
    }
    return 0;
}

/* Build encoder bindings for one candidate form. SYM operands in numeric
 * positions use the symbol's address when known, else a 0 placeholder. */
static int build_bindings(sandstone_asm_t *ctx, const sand_line_t *line,
                          uint32_t form, DomSymAs_OpVal **vals_out,
                          uint32_t *n_out) {
    size_t cap = line->n_operands * 6 + 4;
    DomSymAs_OpVal *vals = calloc(cap, sizeof *vals);
    if (!vals)
        return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < line->n_operands; i++) {
        const sand_operand_t *op = &line->operands[i];
        int is_sym = 0;
        int64_t ival = op->ival;
        const char *sval = op->sval;
        uint32_t ndesc = domsymas_operand_count(ctx->bank, form);
        uint32_t desc = 6;
        if (i < ndesc) {
            const DomSymAs_Operand *d =
                domsymas_operand_at(ctx->bank, form, (uint32_t)i);
            if (d)
                desc = d->kind;
        }
        if (op->kind == SAND_OP_SYM) {
            if (desc == 7) { /* COND: accept `jcc e, target` spellings */
                int64_t cc = 0;
                if (op->sval && cond_code(op->sval, &cc)) {
                    is_sym = 0;
                    ival = cc;
                } else {
                    free(vals);
                    return 0;
                }
            } else if (desc == 1 || desc == 2 || desc == 4) {
                sand_symbol_t *s =
                    op->sval ? sand_find_symbol(ctx, op->sval) : NULL;
                is_sym = 0;
                ival = (s && s->defined) ? (int64_t)s->address : 0;
            } else {
                is_sym = 1;
            }
        } else if (op->kind == SAND_OP_MEM) {
            is_sym = 1;
        }
        char rname[32];
        const char *pnames[6];
        size_t np = 0;
        if (i < 4) {
            for (int k = 0; k < 4; k++)
                pnames[np++] = kPosNames[i][k];
        } else {
            snprintf(rname, sizeof rname, "r%zu", i);
            pnames[np++] = rname;
        }
        if (op->kind == SAND_OP_IMM ||
            (op->kind == SAND_OP_SYM && !is_sym)) {
            pnames[np++] = "imm";
        } else if (op->kind == SAND_OP_SYM) {
            pnames[np++] = "sym";
            pnames[np++] = "target";
        } else if (op->kind == SAND_OP_MEM) {
            pnames[np++] = "mem";
        }
        if ((!is_sym && desc == 7) || op->kind == SAND_OP_COND) {
            pnames[np++] = "cond";
        }
        for (size_t k = 0; k < np; k++) {
            vals[n].name = pnames[k];
            vals[n].is_symbol = is_sym;
            vals[n].ival = ival;
            vals[n].sval = sval;
            n++;
        }
    }
    *vals_out = vals;
    *n_out = n;
    return 1;
}

/* Resolve the mnemonic (with a lowercase fallback) and return candidate
 * form ids that accept the operand count, features and kinds. */
static int candidate_forms(sandstone_asm_t *ctx, const sand_line_t *line,
                           uint32_t *forms, uint32_t cap, uint32_t *n_out) {
    uint32_t found[64];
    uint32_t n_found = 0;
    DomSymAs_Error e =
        domsymas_resolve(ctx->bank, line->mnemonic,
                         (uint32_t)line->n_operands, ctx->enabled,
                         (uint32_t)ctx->n_enabled, found, 64, &n_found);
    if (e != DOMSYMAS_OK || n_found == 0) {
        /* Case-insensitive fallback for banks like x86 (`MOV`). */
        char *lower = sand_strdup(line->mnemonic);
        if (!lower)
            return 0;
        for (char *p = lower; *p; p++)
            if (*p >= 'A' && *p <= 'Z')
                *p = (char)(*p + 32);
        e = domsymas_resolve(ctx->bank, lower, (uint32_t)line->n_operands,
                             ctx->enabled, (uint32_t)ctx->n_enabled, found,
                             64, &n_found);
        free(lower);
        if (e != DOMSYMAS_OK || n_found == 0)
            return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < n_found && n < cap; i++) {
        uint32_t form = found[i];
        uint32_t ndesc = domsymas_operand_count(ctx->bank, form);
        if (ndesc < line->n_operands)
            continue;
        int ok = 1;
        for (size_t k = 0; k < line->n_operands; k++) {
            const DomSymAs_Operand *d =
                domsymas_operand_at(ctx->bank, form, (uint32_t)k);
            if (!d || !kind_matches(&line->operands[k], d->kind)) {
                ok = 0;
                break;
            }
        }
        if (ok)
            forms[n++] = form;
    }
    *n_out = n;
    return n > 0;
}

/* Trial-encode one line; returns the winning form and byte length. */
static int select_form(sandstone_asm_t *ctx, const sand_line_t *line,
                       uint32_t *form_out, uint32_t *len_out) {
    uint32_t forms[64];
    uint32_t n = 0;
    if (!candidate_forms(ctx, line, forms, 64, &n)) {
        sand_set_error(ctx, "%s:%u: no form for '%s' with %zu operand(s)",
                       line->src_name, line->lineno, line->mnemonic,
                       line->n_operands);
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        DomSymAs_OpVal *vals = NULL;
        uint32_t nvals = 0;
        if (!build_bindings(ctx, line, forms[i], &vals, &nvals))
            continue;
        DomSymAs_Bytes bytes = {NULL, 0};
        DomSymAs_RelocOut *relocs = NULL;
        uint32_t nrelocs = 0;
        DomSymAs_Error e = domsymas_encode(ctx->bank, forms[i], vals, nvals,
                                           &bytes, &relocs, &nrelocs);
        free(vals);
        if (e == DOMSYMAS_OK) {
            *form_out = forms[i];
            *len_out = (uint32_t)bytes.len;
            domsymas_free_bytes(&bytes);
            domsymas_free_relocs(relocs, nrelocs);
            return 1;
        }
        domsymas_free_bytes(&bytes);
        domsymas_free_relocs(relocs, nrelocs);
    }
    sand_set_error(ctx, "%s:%u: cannot encode '%s' (tried %u form(s))",
                   line->src_name, line->lineno, line->mnemonic, n);
    return 0;
}

static uint64_t align_up(uint64_t v, uint64_t align) {
    if (align <= 1)
        return v;
    uint64_t m = v % align;
    return m ? v + (align - m) : v;
}

static uint32_t bank_section_align(sandstone_asm_t *ctx, const char *name) {
    const DomSymAs_SectionInfo *s = domsymas_find_section(ctx->bank, name);
    if (s && s->align)
        return s->align;
    return 1;
}

static int is_equ(const char *m) {
    return strncmp(m, ".equ", 4) == 0 || strncmp(m, ".set", 4) == 0;
}

static int define_label(sandstone_asm_t *ctx, const char *name,
                        uint32_t sec_idx, uint64_t address,
                        const sand_line_t *line, int round) {
    sand_symbol_t *s = sand_find_symbol(ctx, name);
    if (s && s->defined && s->round == round) {
        sand_set_error(ctx, "%s:%u: duplicate label '%s'", line->src_name,
                       line->lineno, name);
        return 0;
    }
    if (!s) {
        sand_symbol_t ns;
        memset(&ns, 0, sizeof ns);
        ns.name = sand_strdup(name);
        if (!ns.name)
            return 0;
        sand_symbol_t *arr = sand_realloc_array(ctx->symbols,
                                               ctx->n_symbols + 1,
                                               sizeof ns);
        if (!arr) {
            free(ns.name);
            return 0;
        }
        ctx->symbols = arr;
        s = &ctx->symbols[ctx->n_symbols++];
        *s = ns;
    }
    free(s->section);
    s->section = sand_strdup(ctx->sections[sec_idx].name);
    if (!s->section)
        return 0;
    s->sec_idx = sec_idx;
    s->address = address; /* section-relative until bases are assigned */
    s->defined = 1;
    s->round = round;
    return 1;
}

static int fits(int64_t v, uint32_t width_bits, uint32_t is_signed) {
    if (width_bits >= 64)
        return 1;
    if (is_signed) {
        int64_t lo = -(1LL << (width_bits - 1));
        int64_t hi = (1LL << (width_bits - 1)) - 1;
        return v >= lo && v <= hi;
    }
    return v >= 0 && (uint64_t)v < (1ULL << width_bits);
}

static void write_field(sand_section_t *sec, uint32_t byte_off,
                        uint32_t nbytes, uint64_t v, int be) {
    for (uint32_t i = 0; i < nbytes; i++) {
        unsigned sh = be ? (nbytes - 1 - i) * 8 : i * 8;
        sec->bytes[byte_off + i] = (uint8_t)((v >> sh) & 0xFF);
    }
}

/* Locate the relocation's field inside the emitted instruction.
 *
 * The byte offset of an occurrence comes from the encoder (which records
 * the relocation after emitting placeholder bytes). Two shapes occur in
 * the shipped specs:
 *  - interior/trailing immediates (x86 `call rel32`): the field lies
 *    inside the emitted bytes;
 *  - whole-instruction PC-relative fields (RISC-V `jal`/`branch`): the
 *    encoder emits the full word first and records the relocation at its
 *    end, so the field is the trailing `width` bits.
 *
 * Returns the normalized byte offset of the field, or -1 when the field
 * cannot be located. */
static int reloc_field(const sand_line_t *line, uint32_t insn_len,
                       const DomSymAs_Reloc *rec, const DomSymAs_RelocOut *ro,
                       uint32_t *field_off_out, uint32_t *field_len_out,
                       sandstone_asm_t *ctx) {
    if (rec->width % 8 != 0) {
        sand_set_error(ctx, "%s:%u: unaligned relocation field '%s'",
                       line->src_name, line->lineno, rec->name);
        return -1;
    }
    uint32_t field_len = rec->width / 8;
    uint32_t field_off = ro->offset;
    if (field_off + field_len > insn_len) {
        if (ro->offset == insn_len && field_len <= insn_len) {
            field_off = insn_len - field_len; /* trailing-field shape */
        } else {
            sand_set_error(ctx, "%s:%u: relocation '%s' outside "
                                "instruction",
                           line->src_name, line->lineno, rec->name);
            return -1;
        }
    }
    *field_off_out = field_off;
    *field_len_out = field_len;
    return 0;
}

/* Resolve one encoder relocation against a local label. Returns 1 when
 * resolved (bytes patched), 0 when it must be kept for a linker, -1 on
 * error. Fields that share bytes with opcode bits (nonzero placeholder)
 * are never patched: they need relocation-type-specific bit splicing that
 * only a linker performs. */
static int resolve_reloc(sandstone_asm_t *ctx, const sand_line_t *line,
                         uint32_t sec_idx, uint64_t insn_addr,
                         uint32_t insn_len, const DomSymAs_RelocOut *ro) {
    const DomSymAs_Reloc *rec = NULL;
    if (ro->type < domsymas_reloc_count(ctx->bank))
        rec = domsymas_reloc_at(ctx->bank, ro->type);
    if (!rec) {
        sand_set_error(ctx, "%s:%u: bad relocation type %u", line->src_name,
                       line->lineno, ro->type);
        return -1;
    }
    sand_symbol_t *s = sand_find_symbol(ctx, ro->symbol ? ro->symbol : "");
    if (!s || !s->defined)
        return 0; /* external: kept */
    uint32_t field_off = 0, field_len = 0;
    if (reloc_field(line, insn_len, rec, ro, &field_off, &field_len,
                    ctx) != 0)
        return -1;
    sand_section_t *sec = &ctx->sections[sec_idx];
    uint32_t base = (uint32_t)(sec->len - insn_len);
    for (uint32_t i = 0; i < field_len; i++)
        if (sec->bytes[base + field_off + i] != 0)
            return 0; /* opcode bits share the field: needs a linker */
    uint64_t S = s->address;
    int64_t A = ro->addend;
    int64_t v;
    if (rec->pcrel) {
        int suffix = field_off > 0 &&
                     field_off + field_len == insn_len;
        uint64_t P = suffix ? insn_addr + insn_len : insn_addr;
        v = (int64_t)(S + (uint64_t)A) - (int64_t)P;
    } else {
        v = (int64_t)(S + (uint64_t)A);
    }
    if (!rec->overflow && !fits(v, rec->width, rec->is_signed)) {
        sand_set_error(ctx, "%s:%u: relocation '%s' overflows (%lld)",
                       line->src_name, line->lineno, rec->name,
                       (long long)v);
        return -1;
    }
    uint64_t mask =
        rec->width >= 64 ? ~0ULL : ((1ULL << rec->width) - 1);
    write_field(sec, base + field_off, field_len, (uint64_t)v & mask,
                ctx->byte_order);
    return 1;
}

static int keep_reloc(sandstone_asm_t *ctx, const sand_line_t *line,
                      uint32_t sec_idx, uint32_t insn_base,
                      uint32_t insn_len, const DomSymAs_RelocOut *ro) {
    const DomSymAs_Reloc *rec = NULL;
    if (ro->type < domsymas_reloc_count(ctx->bank))
        rec = domsymas_reloc_at(ctx->bank, ro->type);
    uint32_t field_off = ro->offset, field_len = rec ? rec->width / 8 : 0;
    if (rec) {
        if (reloc_field(line, insn_len, rec, ro, &field_off, &field_len,
                        ctx) != 0)
            return 0;
    }
    sand_reloc_intern_t *arr = sand_realloc_array(ctx->relocs,
                                                 ctx->n_relocs + 1,
                                                 sizeof *arr);
    if (!arr)
        return 0;
    ctx->relocs = arr;
    sand_reloc_intern_t *r = &ctx->relocs[ctx->n_relocs++];
    memset(r, 0, sizeof *r);
    r->sec_idx = sec_idx;
    r->offset = insn_base + field_off;
    r->symbol = sand_strdup(ro->symbol ? ro->symbol : "");
    r->addend = ro->addend;
    r->type = sand_strdup(rec ? rec->name : "UNKNOWN");
    r->width_bits = rec ? rec->width : 0;
    r->pcrel = rec ? (int)rec->pcrel : 0;
    return r->symbol && r->type;
}

/* Grow the per-section offset vector to cover all current sections. */
static int ensure_off(uint64_t **off, size_t *cap, size_t n) {
    if (*cap >= n)
        return 1;
    size_t ncap = *cap ? *cap * 2 : 4;
    while (ncap < n)
        ncap *= 2;
    uint64_t *narr = realloc(*off, ncap * sizeof *narr);
    if (!narr)
        return 0;
    for (size_t i = *cap; i < ncap; i++)
        narr[i] = 0;
    *off = narr;
    *cap = ncap;
    return 1;
}

/* One layout round: `.equ` fixpoint plus address/size assignment for
 * every line (section-relative). Size evaluation that depends on not-yet-
 * defined symbols defers to a later round instead of failing, unless this
 * is the last round. Returns the per-section offsets (caller-owned). */
static int layout_round(sandstone_asm_t *ctx, int round, int last,
                        uint64_t **off_out, int *deferred_out) {
    uint64_t *off = NULL;
    size_t off_cap = 0;
    int deferred = 0;
    char first_err[sizeof ctx->error];
    first_err[0] = '\0';
    uint32_t cur = 0; /* .text is created first */

#define DEFER(ln)                                                           \
    do {                                                                    \
        if (first_err[0] == '\0')                                           \
            memcpy(first_err, ctx->error, sizeof first_err);                \
        deferred = 1;                                                       \
        if (last) {                                                         \
            free(off);                                                      \
            return 0;                                                       \
        }                                                                   \
    } while (0)

    if (!ensure_off(&off, &off_cap, ctx->n_sections)) {
        return 0;
    }

    /* `.equ`/`.set` to a fixpoint (forward references allowed). A hard
     * failure (such as a conflicting redefinition) aborts at once;
     * deferred expressions simply wait for the next outer round. */
    for (int sub = 0; sub < 8; sub++) {
        size_t defined = 0;
        for (size_t k = 0; k < ctx->n_symbols; k++)
            defined += ctx->symbols[k].defined ? 1 : 0;
        for (size_t i = 0; i < ctx->n_lines; i++) {
            sand_line_t *ln = &ctx->lines[i];
            if (!sand_directive_is_symbol(ln) || !is_equ(ln->mnemonic))
                continue;
            if (!sand_directive_symbol(ctx, ln, round)) {
                free(off);
                return 0;
            }
        }
        size_t defined2 = 0;
        for (size_t k = 0; k < ctx->n_symbols; k++)
            defined2 += ctx->symbols[k].defined ? 1 : 0;
        if (defined2 == defined)
            break;
    }

    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        const char *sec_name = NULL;
        int is_bss = 0;
        if (ln->kind == SAND_LINE_DIRECTIVE &&
            sand_directive_section(ln, &sec_name, &is_bss)) {
            /* Section switch: belongs to the previous section, size 0. */
            ln->section_idx = cur;
            ln->size = 0;
            ln->address = off[cur];
            if (!sand_require_section(ctx, sec_name) ||
                !ensure_off(&off, &off_cap, ctx->n_sections)) {
                free(off);
                return 0;
            }
            for (size_t k = 0; k < ctx->n_sections; k++)
                if (strcmp(ctx->sections[k].name, sec_name) == 0) {
                    cur = (uint32_t)k;
                    break;
                }
            ctx->sections[cur].is_bss = is_bss;
            ctx->sections[cur].align = bank_section_align(ctx, sec_name);
            continue;
        }
        ln->section_idx = cur;
        if (ln->label) {
            if (!define_label(ctx, ln->label, cur, off[cur], ln, round)) {
                free(off);
                return 0;
            }
        }
        if (ln->kind == SAND_LINE_EMPTY) {
            ln->size = 0;
            ln->address = off[cur];
        } else if (ln->kind == SAND_LINE_DIRECTIVE) {
            if (sand_directive_is_symbol(ln)) {
                ln->size = 0;
                ln->address = off[cur];
            } else if (strcmp(ln->mnemonic, ".align") == 0 ||
                       strcmp(ln->mnemonic, ".balign") == 0 ||
                       strcmp(ln->mnemonic, ".p2align") == 0 ||
                       strcmp(ln->mnemonic, ".org") == 0) {
                int pad = sand_directive_pad(ctx, ln, cur, off[cur], 0);
                if (pad < 0) {
                    DEFER(ln);
                    pad = 0;
                }
                ln->size = (uint32_t)pad;
                ln->address = off[cur];
                off[cur] += (uint64_t)pad;
            } else {
                if (ctx->sections[cur].is_bss &&
                    strcmp(ln->mnemonic, ".zero") != 0 &&
                    strcmp(ln->mnemonic, ".space") != 0 &&
                    strcmp(ln->mnemonic, ".skip") != 0) {
                    sand_set_error(ctx, "%s:%u: '%s' not allowed in .bss",
                                   ln->src_name, ln->lineno, ln->mnemonic);
                    free(off);
                    return 0;
                }
                uint32_t sz = 0;
                if (!sand_directive_size(ctx, ln, &sz)) {
                    DEFER(ln);
                    sz = 0;
                }
                ln->size = sz;
                ln->address = off[cur];
                off[cur] += sz;
            }
        } else { /* instruction */
            if (ctx->sections[cur].is_bss) {
                sand_set_error(ctx, "%s:%u: code in .bss", ln->src_name,
                               ln->lineno);
                free(off);
                return 0;
            }
            uint32_t form = 0, len = 0;
            if (!select_form(ctx, ln, &form, &len)) {
                DEFER(ln);
                ln->form_id = 0;
                ln->has_form = 0;
                ln->size = 0;
                ln->address = off[cur];
                continue;
            }
            ln->form_id = form;
            ln->has_form = 1;
            ln->size = len;
            ln->address = off[cur];
            off[cur] += len;
        }
    }

#undef DEFER
    *off_out = off;
    *deferred_out = deferred;
    if (deferred && !last && first_err[0] != '\0')
        memcpy(ctx->error, first_err, sizeof ctx->error);
    return 1;
}

/* FNV-1a snapshot of everything layout depends on. */
static uint64_t layout_hash(sandstone_asm_t *ctx, const uint64_t *off) {
    uint64_t h = 1469598103934665603ULL;
#define MIX_BYTE(b)                                                        \
    do {                                                                   \
        h ^= (uint64_t)(uint8_t)(b);                                       \
        h *= 1099511628211ULL;                                             \
    } while (0)
    for (size_t k = 0; k < ctx->n_symbols; k++) {
        const sand_symbol_t *s = &ctx->symbols[k];
        for (const char *p = s->name; *p; p++)
            MIX_BYTE(*p);
        MIX_BYTE(s->defined);
        for (int b = 0; b < 8; b++)
            MIX_BYTE((s->address >> (b * 8)) & 0xFF);
        MIX_BYTE(s->sec_idx & 0xFF);
    }
    for (size_t k = 0; k < ctx->n_sections; k++)
        for (int b = 0; b < 8; b++)
            MIX_BYTE((off[k] >> (b * 8)) & 0xFF);
#undef MIX_BYTE
    return h;
}

int sand_assemble_lines(sandstone_asm_t *ctx) {
    /* ---- pass 1: layout, iterated to convergence ----
     * Later rounds see the labels defined by earlier ones, so `.equ`
     * constants and directive sizes may reference forward labels. */
    if (!sand_require_section(ctx, ".text"))
        return 0;

    /* Order-independent symbol directives first (globals/externals). */
    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        ln->section_idx = 0;
        if (sand_directive_is_symbol(ln) && !is_equ(ln->mnemonic)) {
            if (!sand_directive_symbol(ctx, ln, 0))
                return 0;
        }
    }

    uint64_t *off = NULL;
    uint64_t prev_hash = 0;
    int round;
    int converged = 0;
    for (round = 0; round < 8; round++) {
        int last = round == 7;
        int deferred = 0;
        free(off);
        off = NULL;
        if (!layout_round(ctx, round, last, &off, &deferred)) {
            free(off);
            return 0;
        }
        uint64_t h = layout_hash(ctx, off);
        if (round > 0 && h == prev_hash && !deferred) {
            converged = 1;
            break;
        }
        prev_hash = h;
        if (last && deferred) {
            free(off);
            return 0; /* diagnostic already set */
        }
    }
    if (!converged) {
        sand_set_error(ctx, "layout failed to converge "
                            "(self-referential addresses?)");
        free(off);
        return 0;
    }
    (void)round;

    /* Symbols in numeric (relocation-less) operand positions must be
     * defined: unlike SYM/REL positions they produce no relocation, so an
     * undefined name would silently assemble as zero. */
    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        if (ln->kind != SAND_LINE_INSN || !ln->has_form)
            continue;
        uint32_t ndesc = domsymas_operand_count(ctx->bank, ln->form_id);
        for (size_t k = 0; k < ln->n_operands && k < ndesc; k++) {
            const sand_operand_t *op = &ln->operands[k];
            if (op->kind != SAND_OP_SYM || !op->sval)
                continue;
            const DomSymAs_Operand *d =
                domsymas_operand_at(ctx->bank, ln->form_id, (uint32_t)k);
            /* IMM/DISP/ABS positions emit no relocation: the name must
             * be defined here. SYM/REL positions keep a relocation
             * when the symbol is undefined (obj output). */
            if (!d || (d->kind != 1 && d->kind != 2 && d->kind != 4))
                continue;
            sand_symbol_t *s = sand_find_symbol(ctx, op->sval);
            if (!s || !s->defined) {
                sand_set_error(ctx, "%s:%u: undefined symbol '%s'",
                               ln->src_name, ln->lineno, op->sval);
                free(off);
                return 0;
            }
        }
    }

    /* Assign section bases sequentially. */
    uint64_t base = 0;
    for (size_t k = 0; k < ctx->n_sections; k++) {
        base = align_up(base, ctx->sections[k].align
                                  ? ctx->sections[k].align
                                  : 1);
        ctx->sections[k].base = base;
        base += off[k];
    }
    /* Absolute addresses for labels and lines. */
    for (size_t k = 0; k < ctx->n_symbols; k++) {
        sand_symbol_t *s = &ctx->symbols[k];
        if (s->defined && s->section[0] != '*' && s->section[0] != '\0')
            s->address += ctx->sections[s->sec_idx].base;
    }
    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        ln->address += ctx->sections[ln->section_idx].base;
    }

    /* ---- pass 2: emit ---- */
    for (size_t k = 0; k < ctx->n_sections; k++)
        ctx->sections[k].len = 0;
    for (size_t i = 0; i < ctx->n_lines; i++) {
        sand_line_t *ln = &ctx->lines[i];
        uint32_t sec_idx = ln->section_idx;
        const char *sec_name = NULL;
        int is_bss = 0;
        if (ln->kind == SAND_LINE_DIRECTIVE &&
            sand_directive_section(ln, &sec_name, &is_bss))
            continue;
        if (ln->kind == SAND_LINE_EMPTY)
            continue;
        if (ln->kind == SAND_LINE_DIRECTIVE &&
            sand_directive_is_symbol(ln))
            continue;
        if (ln->kind == SAND_LINE_DIRECTIVE &&
            (strcmp(ln->mnemonic, ".align") == 0 ||
             strcmp(ln->mnemonic, ".balign") == 0 ||
             strcmp(ln->mnemonic, ".p2align") == 0 ||
             strcmp(ln->mnemonic, ".org") == 0)) {
            uint64_t addr = ctx->sections[sec_idx].base +
                            ctx->sections[sec_idx].len;
            size_t before = ctx->sections[sec_idx].len;
            int pad = sand_directive_pad(ctx, ln, sec_idx, addr, 1);
            if (pad < 0) {
                free(off);
                return 0;
            }
            if (ctx->sections[sec_idx].is_bss) {
                /* BSS carries no bytes: rewind the storage, keep the
                 * pass-1 size by advancing the counter only. */
                ctx->sections[sec_idx].len = before + (size_t)pad;
            }
            continue;
        }
        if (ln->kind == SAND_LINE_DIRECTIVE) {
            if (ctx->sections[sec_idx].is_bss) {
                /* .zero/.space in BSS: count without storing. */
                size_t before = ctx->sections[sec_idx].len;
                if (!sand_emit_directive(ctx, ln, sec_idx,
                                         ctx->sections[sec_idx].base +
                                             before)) {
                    free(off);
                    return 0;
                }
                ctx->sections[sec_idx].len = before + ln->size;
                continue;
            }
            if (!sand_emit_directive(ctx, ln, sec_idx,
                                     ctx->sections[sec_idx].base +
                                         ctx->sections[sec_idx].len)) {
                free(off);
                return 0;
            }
            continue;
        }
        /* Instruction: re-encode and resolve relocations. */
        DomSymAs_OpVal *vals = NULL;
        uint32_t nvals = 0;
        if (!build_bindings(ctx, ln, ln->form_id, &vals, &nvals)) {
            free(off);
            return 0;
        }
        DomSymAs_Bytes bytes = {NULL, 0};
        DomSymAs_RelocOut *relocs = NULL;
        uint32_t nrelocs = 0;
        DomSymAs_Error e =
            domsymas_encode(ctx->bank, ln->form_id, vals, nvals, &bytes,
                            &relocs, &nrelocs);
        free(vals);
        if (e != DOMSYMAS_OK) {
            sand_set_error(ctx, "%s:%u: encode failed", ln->src_name,
                           ln->lineno);
            domsymas_free_bytes(&bytes);
            domsymas_free_relocs(relocs, nrelocs);
            free(off);
            return 0;
        }
        sand_section_t *sec = &ctx->sections[sec_idx];
        uint64_t insn_addr = sec->base + sec->len;
        size_t need = sec->len + bytes.len;
        if (need > sec->cap) {
            size_t ncap = sec->cap ? sec->cap * 2 : 64;
            while (ncap < need)
                ncap *= 2;
            uint8_t *nb = realloc(sec->bytes, ncap);
            if (!nb) {
                domsymas_free_bytes(&bytes);
                domsymas_free_relocs(relocs, nrelocs);
                free(off);
                return 0;
            }
            sec->bytes = nb;
            sec->cap = ncap;
        }
        memcpy(sec->bytes + sec->len, bytes.data, bytes.len);
        sec->len += bytes.len;
        for (uint32_t r = 0; r < nrelocs; r++) {
            int rc = resolve_reloc(ctx, ln, sec_idx, insn_addr,
                                   (uint32_t)bytes.len, &relocs[r]);
            if (rc < 0) {
                domsymas_free_bytes(&bytes);
                domsymas_free_relocs(relocs, nrelocs);
                free(off);
                return 0;
            }
            if (rc == 0) {
                uint32_t base =
                    (uint32_t)(sec->len - bytes.len);
                if (!keep_reloc(ctx, ln, sec_idx, base,
                                (uint32_t)bytes.len, &relocs[r])) {
                    domsymas_free_bytes(&bytes);
                    domsymas_free_relocs(relocs, nrelocs);
                    free(off);
                    return 0;
                }
            }
        }
        domsymas_free_bytes(&bytes);
        domsymas_free_relocs(relocs, nrelocs);
    }
    /* BSS carries no initialized bytes: materialize the counted size as
     * zeros so section readers never over-read the storage. */
    for (size_t k = 0; k < ctx->n_sections; k++) {
        sand_section_t *sec = &ctx->sections[k];
        if (!sec->is_bss)
            continue;
        if (sec->len > sec->cap) {
            uint8_t *nb = realloc(sec->bytes, sec->len);
            if (!nb) {
                free(off);
                return 0;
            }
            sec->bytes = nb;
            sec->cap = sec->len;
        }
        memset(sec->bytes, 0, sec->len);
    }
    free(off);
    return 1;
}
