/* DomSymAs — SymAS databank library (C ABI).
 *
 * SymAS binary format, magic 0xCEEB09, extension `.symas`: a compressed,
 * extensible description of an assembler target (mnemonics, opcodes,
 * operands, Algol-like encoding functions, targets, syntaxes, sections,
 * linking, relocations).
 *
 * Ownership rules (stable across versions):
 *  - DomSymAs_Bank handles are owned by the caller; release with
 *    domsymas_close().
 *  - All `const` pointers returned by getters (strings, id arrays, record
 *    structs) are BORROWED from the databank and remain valid until the
 *    databank is closed. Do not free them.
 *  - Buffers explicitly allocated by encode/disassemble
 *    (`DomSymAs_Bytes`, `DomSymAs_RelocOut`, candidate arrays) are OWNED by
 *    the caller and must be released with domsymas_free().
 *  - Error strings from domsymas_last_error() are borrowed; copy them if
 *    they must outlive the next library call.
 */
#ifndef DOMSYMAS_H
#define DOMSYMAS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Magic bytes of a `.symas` file (stored raw, in this order). */
#define DOMSYMAS_MAGIC_0 0xCEu
#define DOMSYMAS_MAGIC_1 0xEBu
#define DOMSYMAS_MAGIC_2 0x09u
#define DOMSYMAS_MAGIC ((uint32_t)0xCEEB09u)

/* Current databank format version produced by this library.
 * 1.0: target/registers/features/mnemonics/opcodes/operands/encoders/
 *      syntax/sections/linking/relocations/index/debug.
 * 1.1: additive optional sections ISEL/SCHED/UNITS/MODELS/TUNING/FUSION
 *      (instruction selection, scheduling, pipeline units, per-model
 *      machine models and optimization hints, instruction-pair fusion).
 *      Section ids 15-20 and directory flag DOMSYMAS_DIRFLAG_OPTIONAL are
 *      new; readers that do not know an optional section id must skip it
 *      using its recorded size. */
#define DOMSYMAS_VERSION_MAJOR 1u
#define DOMSYMAS_VERSION_MINOR 1u
#define DOMSYMAS_VERSION ((uint16_t)0x0001u)

/* Lowest reader version able to interpret every section this build emits. */
#define DOMSYMAS_MIN_READER ((uint16_t)0x0101u)

/* Compression identifiers (file-header `compression` field). */
#define DOMSYMAS_COMP_NONE 0u
#define DOMSYMAS_COMP_ZLIB 1u
#define DOMSYMAS_COMP_ZSTD 2u

/* Header flag bits. */
#define DOMSYMAS_FLAG_BIG_ENDIAN 0x0001u
#define DOMSYMAS_FLAG_CHECKSUM 0x0002u
#define DOMSYMAS_FLAG_HAS_INDEX 0x0004u
#define DOMSYMAS_FLAG_BODY_RELATIVE 0x0008u
#define DOMSYMAS_FLAG_SHARED_STRINGS 0x0010u

/* Stable section identifiers. */
#define DOMSYMAS_SEC_STRINGS 1u
#define DOMSYMAS_SEC_TARGET 2u
#define DOMSYMAS_SEC_REGISTERS 3u
#define DOMSYMAS_SEC_FEATURES 4u
#define DOMSYMAS_SEC_MNEMONICS 5u
#define DOMSYMAS_SEC_OPCODES 6u
#define DOMSYMAS_SEC_OPERANDS 7u
#define DOMSYMAS_SEC_ENCODERS 8u
#define DOMSYMAS_SEC_SYNTAX 9u
#define DOMSYMAS_SEC_SECTIONS 10u
#define DOMSYMAS_SEC_LINKING 11u
#define DOMSYMAS_SEC_RELOCATIONS 12u
#define DOMSYMAS_SEC_INDEX 13u
#define DOMSYMAS_SEC_DEBUG 14u
/* Optional optimization sections (format 1.1+; see AGENTS.md). */
#define DOMSYMAS_SEC_ISEL 15u   /* instruction-selection records */
#define DOMSYMAS_SEC_SCHED 16u  /* scheduling records (per form) */
#define DOMSYMAS_SEC_UNITS 17u  /* pipeline / functional-unit resources */
#define DOMSYMAS_SEC_MODELS 18u /* per-model (subtarget) machine models */
#define DOMSYMAS_SEC_TUNING 19u /* per-model optimization hints */
#define DOMSYMAS_SEC_FUSION 20u /* instruction-pair fusion opportunities */
#define DOMSYMAS_SEC_MAX 20u

/* Section-directory entry flags. */
#define DOMSYMAS_DIRFLAG_INDEX 0x0001u    /* body is the lookup index */
#define DOMSYMAS_DIRFLAG_OPTIONAL 0x0002u /* unknown id => skip by size */

/* Operand kinds. */
#define DOMSYMAS_OP_REG 0u
#define DOMSYMAS_OP_IMM 1u
#define DOMSYMAS_OP_DISP 2u
#define DOMSYMAS_OP_MEM 3u
#define DOMSYMAS_OP_ABS 4u
#define DOMSYMAS_OP_REL 5u
#define DOMSYMAS_OP_SYM 6u
#define DOMSYMAS_OP_COND 7u
#define DOMSYMAS_OP_REGLIST 8u
#define DOMSYMAS_OP_IMPLICIT 9u

/* Register classes. */
#define DOMSYMAS_RC_GPR 0u
#define DOMSYMAS_RC_SEG 1u
#define DOMSYMAS_RC_CTRL 2u
#define DOMSYMAS_RC_DEBUG 3u
#define DOMSYMAS_RC_VEC 4u
#define DOMSYMAS_RC_MASK 5u
#define DOMSYMAS_RC_FP 6u
#define DOMSYMAS_RC_STATUS 7u
#define DOMSYMAS_RC_SPECIAL 8u

/* Register flags. */
#define DOMSYMAS_REG_READABLE 0x01u
#define DOMSYMAS_REG_WRITABLE 0x02u
#define DOMSYMAS_REG_IMP_USED 0x04u
#define DOMSYMAS_REG_IMP_MOD 0x08u
#define DOMSYMAS_REG_MODEDEP 0x10u

/* Instruction-form flags. */
#define DOMSYMAS_IF_PRIVILEGED 0x0001u
#define DOMSYMAS_IF_BRANCH 0x0002u
#define DOMSYMAS_IF_COND_BRANCH 0x0004u
#define DOMSYMAS_IF_RETURN 0x0008u
#define DOMSYMAS_IF_TERMINATOR 0x0010u
#define DOMSYMAS_IF_MAY_TRAP 0x0020u
#define DOMSYMAS_IF_READS_MEM 0x0040u
#define DOMSYMAS_IF_WRITES_MEM 0x0080u
#define DOMSYMAS_IF_DELAYED 0x0100u
#define DOMSYMAS_IF_IMPLICIT_OPS 0x0200u
#define DOMSYMAS_IF_RELOC 0x0400u
#define DOMSYMAS_IF_ALIASES 0x0800u

/* Section (object-section) flags. */
#define DOMSYMAS_SF_EXEC 0x0001u
#define DOMSYMAS_SF_WRITE 0x0002u
#define DOMSYMAS_SF_ALLOC 0x0004u
#define DOMSYMAS_SF_READONLY 0x0008u
#define DOMSYMAS_SF_ZEROFILL 0x0010u
#define DOMSYMAS_SF_MERGE 0x0020u
#define DOMSYMAS_SF_DISCARD 0x0040u
#define DOMSYMAS_SF_TLS 0x0080u
#define DOMSYMAS_SF_DEBUG 0x0100u
#define DOMSYMAS_SF_CODE 0x0200u
#define DOMSYMAS_SF_DATA 0x0400u
#define DOMSYMAS_SF_META 0x0800u

/* Symbol bindings / visibility / types. */
#define DOMSYMAS_BIND_LOCAL 0u
#define DOMSYMAS_BIND_GLOBAL 1u
#define DOMSYMAS_BIND_WEAK 2u
#define DOMSYMAS_BIND_UNDEF 3u
#define DOMSYMAS_BIND_ABS 4u
#define DOMSYMAS_BIND_COMMON 5u
#define DOMSYMAS_BIND_TLS 6u
#define DOMSYMAS_VIS_DEFAULT 0u
#define DOMSYMAS_VIS_HIDDEN 1u
#define DOMSYMAS_VIS_PROTECTED 2u
#define DOMSYMAS_TYPE_NONE 0u
#define DOMSYMAS_TYPE_OBJECT 1u
#define DOMSYMAS_TYPE_FUNC 2u
#define DOMSYMAS_TYPE_SECTION 3u

/* Relocation addend kinds. */
#define DOMSYMAS_ADDEND_EXPLICIT 0u
#define DOMSYMAS_ADDEND_INFIELD 1u
#define DOMSYMAS_ADDEND_IMPLIED 2u

/* Operand access. */
#define DOMSYMAS_ACC_READ 1u
#define DOMSYMAS_ACC_WRITE 2u
#define DOMSYMAS_ACC_READWRITE 3u

/* -------------------------------------------------------------------------
 * Optimization sections (format 1.1+). These describe *how a target prefers
 * to be used*, not how it encodes: which form an operation should lower to,
 * what a form costs, how long it takes, which pipeline resources it occupies,
 * which CPU models exist, what each model changes, and which instruction
 * pairs may be fused. Every section is optional; a consumer that does not
 * implement one simply gets empty vectors.
 * ------------------------------------------------------------------------- */

/* Instruction-selection record kinds (DOMSYMAS_SEC_ISEL). */
#define DOMSYMAS_ISEL_NODE 0u    /* target op -> form (one instruction) */
#define DOMSYMAS_ISEL_PATTERN 1u /* tree pattern -> form (several ops) */
#define DOMSYMAS_ISEL_PSEUDO 2u  /* pseudo instruction -> implementing form */
#define DOMSYMAS_ISEL_EXPANSION 3u /* form -> smaller forms (post-isel) */
#define DOMSYMAS_ISEL_SELECT 4u    /* select (cmov / masked move) preference */
#define DOMSYMAS_ISEL_POSTISEL 5u  /* post-isel intrinsic -> form */
#define DOMSYMAS_ISEL_COPY 6u      /* register copy coalescing class */
#define DOMSYMAS_ISEL_AGGRESSIVE 7u /* source-order / aggressive rewrite */

/* Instruction-selection flags. */
#define DOMSYMAS_IF_PREFER 0x00000001u
#define DOMSYMAS_IF_EXPAND 0x00000002u
#define DOMSYMAS_IF_NEEDS_EXPAND 0x00000004u
#define DOMSYMAS_IF_VARIADIC 0x00000008u
#define DOMSYMAS_IF_COMMUTES 0x00000010u
#define DOMSYMAS_IF_CHAIN 0x00000020u
#define DOMSYMAS_IF_SIDE_EFFECTS 0x00000040u
#define DOMSYMAS_IF_UNPROFITABLE 0x00000080u
#define DOMSYMAS_IF_CONVERGENT 0x00000100u
#define DOMSYMAS_IF_TIED 0x00000200u /* equal-cost alternative */

/* Scheduling flags (DOMSYMAS_SEC_SCHED). */
#define DOMSYMAS_SCH_VARIADIC 0x0001u    /* latency varies with operands */
#define DOMSYMAS_SCH_MULTICYCLE 0x0002u  /* occupancy spans N cycles */
#define DOMSYMAS_SCH_SERIALIZING 0x0004u /* drains the machine */
#define DOMSYMAS_SCH_BARRIER 0x0008u     /* scheduling barrier */
#define DOMSYMAS_SCH_MAY_FAULT 0x0010u   /* may raise (load/store) */
#define DOMSYMAS_SCH_SIDE_EFFECTS 0x0020u
#define DOMSYMAS_SCH_FUSED 0x0040u     /* has a fused counterpart */
#define DOMSYMAS_SCH_UNMODELED 0x0080u /* no per-form record (use model) */
#define DOMSYMAS_SCH_NOP 0x0100u /* executes as a nop when predicate false */

/* Functional-unit kinds (DOMSYMAS_SEC_UNITS). */
#define DOMSYMAS_UK_OTHER 0u
#define DOMSYMAS_UK_INT 1u
#define DOMSYMAS_UK_FP 2u
#define DOMSYMAS_UK_LOAD 3u
#define DOMSYMAS_UK_STORE 4u
#define DOMSYMAS_UK_BRANCH 5u
#define DOMSYMAS_UK_VECTOR 6u
#define DOMSYMAS_UK_MASK 7u
#define DOMSYMAS_UK_DIVIDER 8u
#define DOMSYMAS_UK_MUL 9u
#define DOMSYMAS_UK_COMPLEX 10u
#define DOMSYMAS_UK_PIPE 11u
#define DOMSYMAS_UK_SYSTEM 12u

/* Functional-unit flags. */
#define DOMSYMAS_UF_SHARED 0x0001u      /* shared with another model */
#define DOMSYMAS_UF_SUPERSCALAR 0x0002u /* issues more than one op/cycle */
#define DOMSYMAS_UF_PIPELINED 0x0004u   /* pipelined (throughput < latency) */
#define DOMSYMAS_UF_NONCOINCIDENT 0x0008u /* non-coincident scheduling */

/* Machine-model flags (DOMSYMAS_SEC_MODELS). */
#define DOMSYMAS_MF_IN_ORDER 0x00000001u
#define DOMSYMAS_MF_POST_RA 0x00000002u
#define DOMSYMAS_MF_COMPLETE 0x00000004u /* every form is modeled */
#define DOMSYMAS_MF_PREFER_POST_RAM 0x00000008u
#define DOMSYMAS_MF_NO_ITINERARIES 0x00000010u
#define DOMSYMAS_MF_VLIW 0x00000020u
#define DOMSYMAS_MF_PREDICABLE 0x00000040u
#define DOMSYMAS_MF_MORPHABLE 0x00000080u /* DVFS/morphing core */
#define DOMSYMAS_MF_HAS_STALLS 0x00000100u
#define DOMSYMAS_MF_FORWARDING 0x00000200u /* operand forwarding */
#define DOMSYMAS_MF_OUT_OF_ORDER 0x00000400u
#define DOMSYMAS_MF_BUNDLE_WIDTH 0x00000800u /* IssueWidth == bundle width */
#define DOMSYMAS_MF_DEFAULT 0x00001000u /* used when no model is selected */

/* Tuning-hint kinds (DOMSYMAS_SEC_TUNING). */
#define DOMSYMAS_TK_GENERIC 0u
#define DOMSYMAS_TK_LATENCY 1u
#define DOMSYMAS_TK_UOPS 2u
#define DOMSYMAS_TK_COST 3u
#define DOMSYMAS_TK_PREFER 4u /* raise selection priority */
#define DOMSYMAS_TK_AVOID 5u  /* deprioritize / suppress */
#define DOMSYMAS_TK_THRESHOLD 6u
#define DOMSYMAS_TK_UNROLL 7u
#define DOMSYMAS_TK_VEC_WIDTH 8u
#define DOMSYMAS_TK_PAIR 9u      /* form pair hint (form/form2) */
#define DOMSYMAS_TK_STRATEGY 10u /* scheduling strategy id */
#define DOMSYMAS_TK_PREROLL 11u
#define DOMSYMAS_TK_SPLIT 12u

/* Tuning-hint flags. */
#define DOMSYMAS_TF_ONLY_IF 0x0001u  /* applies when `features` hold */
#define DOMSYMAS_TF_OVERRIDE 0x0002u /* replaces the base value */
#define DOMSYMAS_TF_EXPERIMENTAL 0x0004u
#define DOMSYMAS_TF_DISABLE 0x0008u
#define DOMSYMAS_TF_NEGATIVE 0x0010u /* value is a decrease */

/* Fusion kinds (DOMSYMAS_SEC_FUSION). */
#define DOMSYMAS_FK_MACRO 0u       /* generic macro-fusion pair */
#define DOMSYMAS_FK_CMP_BRANCH 1u  /* compare + conditional branch */
#define DOMSYMAS_FK_BRANCH_PAIR 2u /* load + branch */
#define DOMSYMAS_FK_MEM_ARITH 3u   /* load/store + ALU */
#define DOMSYMAS_FK_MUL_ADD 4u     /* multiply + add (FMA-style) */
#define DOMSYMAS_FK_SHIFT 5u
#define DOMSYMAS_FK_MOV 6u /* move + ALU (zero-idiom) */
#define DOMSYMAS_FK_LOAD_OP 7u
#define DOMSYMAS_FK_CMP_SELECT 8u /* compare + select */
#define DOMSYMAS_FK_VSETVLI 9u    /* vector-config + user op */
#define DOMSYMAS_FK_TARGET 10u    /* target-specific pair */

/* Fusion flags. */
#define DOMSYMAS_FF_PROFITABLE 0x0001u
#define DOMSYMAS_FF_REQUIRES_FLAGS 0x0002u
#define DOMSYMAS_FF_SAME_REG 0x0004u
#define DOMSYMAS_FF_ADJACENT 0x0008u
#define DOMSYMAS_FF_LOCK_ELIGIBLE 0x0010u
#define DOMSYMAS_FF_SUPPRESS_UNFUSE 0x0020u
#define DOMSYMAS_FF_BIDIRECTIONAL 0x0040u

  typedef struct DomSymAs_Bank DomSymAs_Bank;
  typedef struct DomSymAs_Builder DomSymAs_Builder;

  /* Stable error codes. */
  typedef enum DomSymAs_Error
  {
    DOMSYMAS_OK = 0,
    DOMSYMAS_ERR_IO = 1,
    DOMSYMAS_ERR_NOMEM = 2,
    DOMSYMAS_ERR_BAD_MAGIC = 3,
    DOMSYMAS_ERR_BAD_VERSION = 4,
    DOMSYMAS_ERR_BAD_COMPRESSION = 5,
    DOMSYMAS_ERR_BAD_CHECKSUM = 6,
    DOMSYMAS_ERR_BAD_BOUNDS = 7,
    DOMSYMAS_ERR_BAD_STRING_REF = 8,
    DOMSYMAS_ERR_BAD_RECORD_REF = 9,
    DOMSYMAS_ERR_BAD_FORM_REF = 10,
    DOMSYMAS_ERR_BAD_ENCODER_REF = 11,
    DOMSYMAS_ERR_BAD_FEATURE_REF = 12,
    DOMSYMAS_ERR_BAD_RELOC = 13,
    DOMSYMAS_ERR_BAD_ALIGN = 14,
    DOMSYMAS_ERR_BAD_FLAGS = 15,
    DOMSYMAS_ERR_TRUNCATED = 16,
    DOMSYMAS_ERR_OVERFLOW = 17,
    DOMSYMAS_ERR_ENCODE = 18,
    DOMSYMAS_ERR_NOT_FOUND = 19,
    DOMSYMAS_ERR_UNKNOWN = 20
  } DomSymAs_Error;

  /* Record views. All `const char *` and id-array pointers are BORROWED. */
  typedef struct DomSymAs_Target
  {
    uint32_t id;
    const char *arch;
    const char *subarch;
    uint32_t addr_size;
    uint32_t default_op_size;
    uint32_t default_addr_size;
    uint32_t instr_align;
    uint32_t byte_order; /* 0 = LE, 1 = BE */
    const char *obj_format;
    const char *abi;
    const char *calling_conv;
    const uint32_t *features;
    uint32_t feature_count;
  } DomSymAs_Target;

  typedef struct DomSymAs_Feature
  {
    uint32_t id;
    const char *name;
    uint32_t flags;
    const char *desc;
  } DomSymAs_Feature;

  typedef struct DomSymAs_Register
  {
    uint32_t id;
    const char *name;
    uint32_t regclass;
    uint32_t width;
    uint32_t enc_num;
    uint32_t parent; /* 0xFFFFFFFF = none */
    uint32_t sub_lo;
    uint32_t sub_hi;
    uint32_t flags;
  } DomSymAs_Register;

  typedef struct DomSymAs_Mnemonic
  {
    uint32_t id;
    const char *canonical;
    const char *const *aliases;
    uint32_t alias_count;
    uint32_t flags; /* bit0 = case-insensitive */
    const uint32_t *forms;
    uint32_t form_count;
  } DomSymAs_Mnemonic;

  typedef struct DomSymAs_Form
  {
    uint32_t id;
    uint32_t mnemonic;
    uint32_t operand_count;
    uint32_t flags;
    uint32_t encoder;
    const uint32_t *required_features;
    uint32_t required_count;
    const uint32_t *forbidden_features;
    uint32_t forbidden_count;
    const uint32_t *syntaxes;
    uint32_t syntax_count;
  } DomSymAs_Form;

  typedef struct DomSymAs_Operand
  {
    uint32_t id;
    uint32_t form;
    uint32_t position;
    uint32_t kind;
    uint32_t width;
    uint32_t regclass;
    int64_t imm_min;
    int64_t imm_max;
    uint32_t is_signed;
    uint32_t enc_role;
    uint32_t access;
    uint32_t hidden;
    const char *constraint;
  } DomSymAs_Operand;

  typedef struct DomSymAs_Encoder
  {
    uint32_t id;
    const char *name;
    const char *source;
    uint32_t flags;
    uint32_t max_bytes;
    uint32_t max_relocs;
  } DomSymAs_Encoder;

  typedef struct DomSymAs_Syntax
  {
    uint32_t id;
    const char *name;
    uint32_t operand_order; /* 0 = normal, 1 = reversed */
    const char *reg_prefix;
    const char *imm_prefix;
    const char *comment;
    const char *separator;
    uint32_t case_sensitive;
    const char *tmpl;
    const uint32_t *forms;
    uint32_t form_count;
  } DomSymAs_Syntax;

  typedef struct DomSymAs_SectionInfo
  {
    uint32_t id;
    const char *name;
    uint32_t flags;
    uint32_t align;
    uint32_t addrspace;
    uint32_t content;
  } DomSymAs_SectionInfo;

  typedef struct DomSymAs_Linkage
  {
    uint32_t id;
    const char *name;
    uint32_t binding;
    uint32_t visibility;
    uint32_t symtype;
    uint32_t flags;
  } DomSymAs_Linkage;

  typedef struct DomSymAs_Reloc
  {
    uint32_t id;
    const char *name;
    uint32_t width;  /* field width in bits */
    uint32_t offset; /* field offset in bits */
    uint32_t is_signed;
    uint32_t pcrel;
    uint32_t overflow; /* 0 = error, 1 = truncate */
    uint32_t addend;
    const char *expr;
  } DomSymAs_Reloc;

  /* ---- Optimization records (format 1.1+) ---- */

#define DOMSYMAS_NO_FORM 0xFFFFFFFFu
#define DOMSYMAS_NO_MODEL 0xFFFFFFFFu

  typedef struct DomSymAs_Isel
  {
    uint32_t id;
    uint32_t kind;       /* DOMSYMAS_ISEL_* */
    const char *node;    /* target op / pseudo name */
    uint32_t form;       /* selected form id */
    uint32_t cost;       /* selection cost, 0 = unspecified */
    uint32_t complexity; /* added complexity (preferred when higher) */
    uint32_t priority;   /* tie-break priority, higher wins */
    uint32_t flags;      /* DOMSYMAS_IF_* */
    const uint32_t *required_features;
    uint32_t required_count;
    const uint32_t *forbidden_features;
    uint32_t forbidden_count;
    const uint32_t *alts; /* other forms for the same node, best first */
    uint32_t alt_count;
    const char *pattern; /* optional source-pattern text */
  } DomSymAs_Isel;

  typedef struct DomSymAs_Sched
  {
    uint32_t id;
    uint32_t form;
    const char *name; /* schedule-class name */
    uint32_t latency; /* write latency in cycles (0xFFFFFFFF: variadic) */
    uint32_t uops;    /* micro-ops issued */
    uint32_t cycles;  /* resource cycles occupied (0 = 1) */
    uint32_t flags;   /* DOMSYMAS_SCH_* */
    const uint32_t *writes; /* unit ids (write resources) */
    uint32_t write_count;
    const uint32_t *reads; /* unit ids (read resources) */
    uint32_t read_count;
  } DomSymAs_Sched;

  typedef struct DomSymAs_Unit
  {
    uint32_t id;
    uint32_t model; /* owning model id, DOMSYMAS_NO_MODEL = target-wide */
    const char *name;
    uint32_t kind;       /* DOMSYMAS_UK_* */
    uint32_t count;      /* number of identical units */
    uint32_t latency;    /* default latency in cycles */
    uint32_t throughput; /* reciprocal throughput (cycles per op) */
    uint32_t flags;      /* DOMSYMAS_UF_* */
  } DomSymAs_Unit;

  typedef struct DomSymAs_Model
  {
    uint32_t id;
    const char *name;
    const char *vendor;
    uint32_t issue_width;     /* instructions / micro-ops issued per cycle */
    uint32_t micro_op_buffer; /* reorder / micro-op buffer size */
    uint32_t loop_buffer;     /* loop micro-op buffer size */
    uint32_t load_latency;    /* L1-hit load latency */
    uint32_t mispredict_penalty; /* extra cycles for a mispredict */
    uint32_t high_latency;       /* "high latency" threshold */
    uint32_t high_uops;          /* "high uop count" threshold */
    uint32_t flags;              /* DOMSYMAS_MF_* */
    const uint32_t *features;    /* architectural features enabled */
    uint32_t feature_count;
    const uint32_t *tune_features; /* tuning-only features */
    uint32_t tune_feature_count;
    const char *desc;
  } DomSymAs_Model;

  typedef struct DomSymAs_Tune
  {
    uint32_t id;
    uint32_t model;
    uint32_t kind;    /* DOMSYMAS_TK_* */
    const char *name; /* hint name, e.g. "latency" or "prefer" */
    uint32_t form;    /* subject form id or DOMSYMAS_NO_FORM */
    uint32_t form2;   /* second form for pair hints */
    int64_t value;    /* numeric payload */
    uint32_t flags;   /* DOMSYMAS_TF_* */
    const uint32_t *features;
    uint32_t feature_count;
    const char *desc;
  } DomSymAs_Tune;

  typedef struct DomSymAs_Fusion
  {
    uint32_t id;
    const char *name;
    uint32_t a;       /* first form id */
    uint32_t b;       /* second form id */
    uint32_t kind;    /* DOMSYMAS_FK_* */
    uint32_t latency; /* fused latency when known (0 = unknown) */
    uint32_t flags;   /* DOMSYMAS_FF_* */
    const uint32_t *features;
    uint32_t feature_count;
    const char *pattern; /* predicate summary */
  } DomSymAs_Fusion;

  /* Resolved scheduling answer (base record plus per-model overrides).
   * Caller-owned: release with domsymas_free_sched_info(). */
  typedef struct DomSymAs_SchedInfo
  {
    int found;        /* 0 = no scheduling information */
    uint32_t sched;   /* source schedule record id (kNoId when none) */
    char *name;       /* schedule-class name, caller-owned */
    uint32_t latency; /* cycles; 0xFFFFFFFF = variadic */
    uint32_t uops;
    uint32_t cycles;
    uint32_t flags;
    uint32_t *writes; /* caller-owned unit ids */
    uint32_t write_count;
    uint32_t *reads;
    uint32_t read_count;
    uint32_t cost; /* selection cost */
    int cost_known;
  } DomSymAs_SchedInfo;

  /* Caller-owned encode output. */
  typedef struct DomSymAs_Bytes
  {
    uint8_t *data;
    size_t len;
  } DomSymAs_Bytes;

  typedef struct DomSymAs_RelocOut
  {
    uint32_t type;   /* relocation id */
    uint32_t offset; /* byte offset in emitted code */
    int64_t addend;
    char *symbol; /* caller-owned, NUL-terminated */
  } DomSymAs_RelocOut;

  /* Operand binding for the encoder: integer or symbol value. */
  typedef struct DomSymAs_OpVal
  {
    const char *name; /* operand name, e.g. "dst", "src", "imm" */
    int is_symbol;    /* 0 = integer `ival`, 1 = symbol `sval` */
    int64_t ival;
    const char *sval;
  } DomSymAs_OpVal;

  /* Disassembly candidate (caller-owned array; text pointers borrowed). */
  typedef struct DomSymAs_Candidate
  {
    uint32_t form;
    const char *mnemonic;
    uint32_t consumed; /* bytes consumed */
    char *text;        /* caller-owned rendered text */
  } DomSymAs_Candidate;

  /* Library meta. */
  const char *domsymas_version (void);
  const char *domsymas_error_string (DomSymAs_Error err);
  uint32_t domsymas_magic (void);

  /* Pure-C header probe (no C++ required; see domsymas.c). */
  int domsymas_probe_magic (const uint8_t *data, size_t len);
  uint32_t domsymas_crc32 (const uint8_t *data, size_t len);

  /* Lifecycle. */
  DomSymAs_Error domsymas_open_file (const char *path, DomSymAs_Bank **out);
  DomSymAs_Error domsymas_open_memory (const uint8_t *data, size_t len,
                                       DomSymAs_Bank **out);
  void domsymas_close (DomSymAs_Bank *bank);
  const char *domsymas_last_error (const DomSymAs_Bank *bank);

  /* Validation: 0 = structural only, 1 = structural + semantic. */
  DomSymAs_Error domsymas_validate (DomSymAs_Bank *bank, int semantic);

  /* Header / databank introspection. */
  DomSymAs_Error domsymas_header_info (const DomSymAs_Bank *bank,
                                       uint16_t *version, uint16_t *flags,
                                       uint8_t *compression,
                                       uint32_t *uncompressed_size,
                                       uint32_t *compressed_size);
  uint32_t domsymas_string_count (const DomSymAs_Bank *bank);
  const char *domsymas_string_at (const DomSymAs_Bank *bank, uint32_t index);

  /* Targets / features / registers. */
  uint32_t domsymas_target_count (const DomSymAs_Bank *bank);
  const DomSymAs_Target *domsymas_target_at (const DomSymAs_Bank *bank,
                                             uint32_t i);
  uint32_t domsymas_feature_count (const DomSymAs_Bank *bank);
  const DomSymAs_Feature *domsymas_feature_at (const DomSymAs_Bank *bank,
                                               uint32_t i);
  const DomSymAs_Feature *domsymas_find_feature (const DomSymAs_Bank *bank,
                                                 const char *name);
  uint32_t domsymas_register_count (const DomSymAs_Bank *bank);
  const DomSymAs_Register *domsymas_register_at (const DomSymAs_Bank *bank,
                                                 uint32_t i);
  const DomSymAs_Register *domsymas_find_register (const DomSymAs_Bank *bank,
                                                   const char *name);

  /* Mnemonics / forms / operands / encoders. */
  uint32_t domsymas_mnemonic_count (const DomSymAs_Bank *bank);
  const DomSymAs_Mnemonic *domsymas_mnemonic_at (const DomSymAs_Bank *bank,
                                                 uint32_t i);
  /* Returns up to `cap` form ids for `name` (aliases included); *out_n set. */
  DomSymAs_Error domsymas_find_mnemonic (const DomSymAs_Bank *bank,
                                         const char *name, uint32_t *out,
                                         uint32_t cap, uint32_t *out_n);
  uint32_t domsymas_form_count (const DomSymAs_Bank *bank);
  const DomSymAs_Form *domsymas_form_at (const DomSymAs_Bank *bank,
                                         uint32_t i);
  const DomSymAs_Form *domsymas_find_form (const DomSymAs_Bank *bank,
                                           uint32_t id);
  uint32_t domsymas_operand_count (const DomSymAs_Bank *bank,
                                   uint32_t form_id);
  const DomSymAs_Operand *domsymas_operand_at (const DomSymAs_Bank *bank,
                                               uint32_t form_id, uint32_t i);
  const DomSymAs_Encoder *domsymas_find_encoder (const DomSymAs_Bank *bank,
                                                 uint32_t id);
  const char *domsymas_encoder_source (const DomSymAs_Bank *bank, uint32_t id);

  /* Syntax / sections / linking / relocations. */
  uint32_t domsymas_syntax_count (const DomSymAs_Bank *bank);
  const DomSymAs_Syntax *domsymas_syntax_at (const DomSymAs_Bank *bank,
                                             uint32_t i);
  const DomSymAs_Syntax *domsymas_find_syntax (const DomSymAs_Bank *bank,
                                               const char *name);
  uint32_t domsymas_section_count (const DomSymAs_Bank *bank);
  const DomSymAs_SectionInfo *domsymas_section_at (const DomSymAs_Bank *bank,
                                                   uint32_t i);
  const DomSymAs_SectionInfo *domsymas_find_section (const DomSymAs_Bank *bank,
                                                     const char *name);
  uint32_t domsymas_linkage_count (const DomSymAs_Bank *bank);
  const DomSymAs_Linkage *domsymas_linkage_at (const DomSymAs_Bank *bank,
                                               uint32_t i);
  uint32_t domsymas_reloc_count (const DomSymAs_Bank *bank);
  const DomSymAs_Reloc *domsymas_reloc_at (const DomSymAs_Bank *bank,
                                           uint32_t i);
  const DomSymAs_Reloc *domsymas_find_reloc (const DomSymAs_Bank *bank,
                                             const char *name);

  /* Index lookup: kind 0=mnemonic 1=register 2=feature 3=syntax 4=reloc
   * 5=section 6=model 7=iselect-node 8=unit 9=schedule-class 10=fusion-pair.
   * Returns record id or NOT_FOUND. */
  DomSymAs_Error domsymas_index_lookup (const DomSymAs_Bank *bank,
                                        uint32_t kind, const char *key,
                                        uint32_t *record_id);

  /* Feature-aware form resolution: pick forms of `mnemonic` usable when the
   * CPU provides `enabled`/`enabled_n` features and `op_count` operands. */
  DomSymAs_Error domsymas_resolve (const DomSymAs_Bank *bank,
                                   const char *mnemonic, uint32_t op_count,
                                   const uint32_t *enabled, uint32_t enabled_n,
                                   uint32_t *out, uint32_t cap,
                                   uint32_t *out_n);

  /* Encoding: evaluate the form's encoding function. */
  DomSymAs_Error domsymas_encode (const DomSymAs_Bank *bank, uint32_t form_id,
                                  const DomSymAs_OpVal *ops, uint32_t op_n,
                                  DomSymAs_Bytes *out_bytes,
                                  DomSymAs_RelocOut **out_relocs,
                                  uint32_t *out_reloc_n);

  /* Disassembly: match `len` bytes at `code` against known forms. */
  DomSymAs_Error domsymas_disassemble (const DomSymAs_Bank *bank,
                                       const uint8_t *code, size_t len,
                                       uint32_t syntax_id,
                                       DomSymAs_Candidate **out,
                                       uint32_t *out_n);

  /* ---- Instruction selection (DOMSYMAS_SEC_ISEL) ----
   * `node` is the operation name recorded by the producer (for example "add",
   * "vselect" or "COPY"). Records are returned in producer order; the producer
   * is expected to list alternatives best-first. */
  uint32_t domsymas_isel_count (const DomSymAs_Bank *bank);
  const DomSymAs_Isel *domsymas_isel_at (const DomSymAs_Bank *bank,
                                         uint32_t i);
  /* Writes up to `cap` form ids of the records for `node` into `out` and the
   * count into `out_n`; NOT_FOUND when the bank has no record for it. */
  DomSymAs_Error domsymas_find_isel (const DomSymAs_Bank *bank,
                                     const char *node, uint32_t *out,
                                     uint32_t cap, uint32_t *out_n);
  /* Best form for `node` given enabled features and a model (DOMSYMAS_NO_MODEL
   * = any model). Feature predicates and the model's prefer/avoid hints are
   * applied first; ties are broken by priority, then complexity, then cost,
   * then producer order. NOT_FOUND when no record applies. */
  DomSymAs_Error domsymas_select_form (const DomSymAs_Bank *bank,
                                       const char *node,
                                       const uint32_t *enabled,
                                       uint32_t enabled_n, uint32_t model,
                                       uint32_t *out_form);

  /* ---- Scheduling (DOMSYMAS_SEC_SCHED + DOMSYMAS_SEC_TUNING + UNITS) ---- */
  uint32_t domsymas_sched_count (const DomSymAs_Bank *bank);
  const DomSymAs_Sched *domsymas_sched_at (const DomSymAs_Bank *bank,
                                           uint32_t i);
  /* Base (model-independent) scheduling record for a form, or NULL. */
  const DomSymAs_Sched *domsymas_sched_for_form (const DomSymAs_Bank *bank,
                                                 uint32_t form);
  /* Resolved scheduling: base record with the model's TUNING overrides
   * applied. `out` is filled with caller-owned storage; release it with
   * domsymas_free_sched_info(). */
  DomSymAs_Error domsymas_sched_info (const DomSymAs_Bank *bank, uint32_t form,
                                      uint32_t model, DomSymAs_SchedInfo *out);
  void domsymas_free_sched_info (DomSymAs_SchedInfo *si);

  /* ---- Functional units (DOMSYMAS_SEC_UNITS) ---- */
  uint32_t domsymas_unit_count (const DomSymAs_Bank *bank);
  const DomSymAs_Unit *domsymas_unit_at (const DomSymAs_Bank *bank,
                                         uint32_t i);
  const DomSymAs_Unit *domsymas_find_unit (const DomSymAs_Bank *bank,
                                           const char *name);

  /* ---- Machine models (DOMSYMAS_SEC_MODELS) ---- */
  uint32_t domsymas_model_count (const DomSymAs_Bank *bank);
  const DomSymAs_Model *domsymas_model_at (const DomSymAs_Bank *bank,
                                           uint32_t i);
  const DomSymAs_Model *domsymas_find_model (const DomSymAs_Bank *bank,
                                             const char *name);
  /* Id of the model flagged DOMSYMAS_MF_DEFAULT, else the first record. */
  uint32_t domsymas_default_model (const DomSymAs_Bank *bank);

  /* ---- Per-model optimization hints (DOMSYMAS_SEC_TUNING) ---- */
  uint32_t domsymas_tune_count (const DomSymAs_Bank *bank);
  const DomSymAs_Tune *domsymas_tune_at (const DomSymAs_Bank *bank,
                                         uint32_t i);
  /* Value of the hint of `kind` that `model` applies to `form`; NOT_FOUND when
   * the model has no such hint. */
  DomSymAs_Error domsymas_tune_value (const DomSymAs_Bank *bank,
                                      uint32_t model, uint32_t form,
                                      uint32_t kind, int64_t *out);

  /* ---- Instruction-pair fusion (DOMSYMAS_SEC_FUSION) ---- */
  uint32_t domsymas_fusion_count (const DomSymAs_Bank *bank);
  const DomSymAs_Fusion *domsymas_fusion_at (const DomSymAs_Bank *bank,
                                             uint32_t i);
  /* Fusion record for the ordered pair (a, b); tries (b, a) when `bidirect`.
   */
  const DomSymAs_Fusion *domsymas_find_fusion (const DomSymAs_Bank *bank,
                                               uint32_t a, uint32_t b,
                                               int bidirect);

  /* Release caller-owned buffers. */
  void domsymas_free (void *ptr);
  void domsymas_free_bytes (DomSymAs_Bytes *b);
  void domsymas_free_relocs (DomSymAs_RelocOut *r, uint32_t n);
  void domsymas_free_candidates (DomSymAs_Candidate *c, uint32_t n);

  /* Builder (producer side). All strings are copied. */
  DomSymAs_Builder *domsymas_builder_new (int little_endian);
  void domsymas_builder_free (DomSymAs_Builder *b);
  DomSymAs_Error domsymas_builder_set_compression (DomSymAs_Builder *b,
                                                   uint8_t comp);
  DomSymAs_Error domsymas_builder_add_target (
      DomSymAs_Builder *b, const char *arch, const char *subarch,
      uint32_t addr_size, uint32_t op_size, uint32_t addr_def, uint32_t align,
      uint32_t byte_order, const char *objfmt, const char *abi,
      const char *conv, const uint32_t *features, uint32_t feature_n,
      uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_feature (DomSymAs_Builder *b,
                                               const char *name,
                                               uint32_t flags,
                                               const char *desc,
                                               uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_register (
      DomSymAs_Builder *b, const char *name, uint32_t regclass, uint32_t width,
      uint32_t enc_num, uint32_t parent, uint32_t sub_lo, uint32_t sub_hi,
      uint32_t flags, uint32_t *out_id);
  DomSymAs_Error
  domsymas_builder_add_mnemonic (DomSymAs_Builder *b, const char *canonical,
                                 const char *const *aliases, uint32_t alias_n,
                                 uint32_t flags, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_form (
      DomSymAs_Builder *b, uint32_t mnemonic, uint32_t op_count,
      uint32_t flags, uint32_t encoder, const uint32_t *req, uint32_t req_n,
      const uint32_t *forb, uint32_t forb_n, const uint32_t *syntaxes,
      uint32_t syn_n, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_operand (
      DomSymAs_Builder *b, uint32_t form, uint32_t pos, uint32_t kind,
      uint32_t width, uint32_t regclass, int64_t imm_min, int64_t imm_max,
      uint32_t is_signed, uint32_t enc_role, uint32_t access, uint32_t hidden,
      const char *constraint, uint32_t *out_id);
  DomSymAs_Error
  domsymas_builder_add_encoder (DomSymAs_Builder *b, const char *name,
                                const char *source, uint32_t flags,
                                uint32_t max_bytes, uint32_t max_relocs,
                                uint32_t *out_id);
  DomSymAs_Error
  domsymas_builder_add_syntax (DomSymAs_Builder *b, const char *name,
                               uint32_t order, const char *reg_prefix,
                               const char *imm_prefix, const char *comment,
                               const char *sep, uint32_t case_sensitive,
                               const char *tmpl, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_link_syntax_form (DomSymAs_Builder *b,
                                                    uint32_t syntax,
                                                    uint32_t form);
  DomSymAs_Error domsymas_builder_link_mnemonic_form (DomSymAs_Builder *b,
                                                      uint32_t mnemonic,
                                                      uint32_t form);
  DomSymAs_Error domsymas_builder_add_section (
      DomSymAs_Builder *b, const char *name, uint32_t flags, uint32_t align,
      uint32_t addrspace, uint32_t content, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_linkage (
      DomSymAs_Builder *b, const char *name, uint32_t binding,
      uint32_t visibility, uint32_t symtype, uint32_t flags, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_reloc (
      DomSymAs_Builder *b, const char *name, uint32_t width, uint32_t offset,
      uint32_t is_signed, uint32_t pcrel, uint32_t overflow, uint32_t addend,
      const char *expr, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_debug (DomSymAs_Builder *b,
                                             const char *msg,
                                             uint32_t ref_kind,
                                             uint32_t ref_id,
                                             uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_isel (
      DomSymAs_Builder *b, const char *node, uint32_t kind, uint32_t form,
      uint32_t cost, uint32_t complexity, uint32_t priority, uint32_t flags,
      const uint32_t *req, uint32_t req_n, const uint32_t *forb,
      uint32_t forb_n, const uint32_t *alts, uint32_t alt_n,
      const char *pattern, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_sched (
      DomSymAs_Builder *b, uint32_t form, const char *name, uint32_t latency,
      uint32_t uops, uint32_t cycles, uint32_t flags, const uint32_t *writes,
      uint32_t write_n, const uint32_t *reads, uint32_t read_n,
      uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_unit (DomSymAs_Builder *b,
                                            uint32_t model, const char *name,
                                            uint32_t kind, uint32_t count,
                                            uint32_t latency,
                                            uint32_t throughput,
                                            uint32_t flags, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_add_model (
      DomSymAs_Builder *b, const char *name, const char *vendor,
      uint32_t issue_width, uint32_t micro_op_buffer, uint32_t loop_buffer,
      uint32_t load_latency, uint32_t mispredict_penalty,
      uint32_t high_latency, uint32_t high_uops, uint32_t flags,
      const uint32_t *features, uint32_t feature_n, const uint32_t *tune,
      uint32_t tune_n, const char *desc, uint32_t *out_id);
  DomSymAs_Error
  domsymas_builder_add_tune (DomSymAs_Builder *b, uint32_t model,
                             uint32_t kind, const char *name, uint32_t form,
                             uint32_t form2, int64_t value, uint32_t flags,
                             const uint32_t *features, uint32_t feature_n,
                             const char *desc, uint32_t *out_id);
  DomSymAs_Error
  domsymas_builder_add_fusion (DomSymAs_Builder *b, const char *name,
                               uint32_t form_a, uint32_t form_b, uint32_t kind,
                               uint32_t latency, uint32_t flags,
                               const uint32_t *features, uint32_t feature_n,
                               const char *pattern, uint32_t *out_id);
  DomSymAs_Error domsymas_builder_build (DomSymAs_Builder *b,
                                         DomSymAs_Bank **out);
  DomSymAs_Error domsymas_builder_save (DomSymAs_Builder *b, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* DOMSYMAS_H */
