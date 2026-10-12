// DomSymAs — C++ API over the SymAS databank format (magic 0xCEEB09).
//
// Layout summary (normative for this implementation):
//  File:  [FileHeader:23][compressed payload]
//   FileHeader (always little-endian): magic[3]=CE EB 09, version u16,
//     flags u16, compression u8, reserved[3]=0, uncompressed u32,
//     compressed u32, checksum u32 (crc32 of payload iff FLAG_CHECKSUM).
//  Payload (endianness = header FLAG_BIG_ENDIAN ? BE : LE):
//   [db_magic u32 "SDB1"][version u16][section_count u16]
//   [dir entries: id u16, flags u16, offset u32, size u32, align u32,
//                 checksum u32 (crc32, 0 = unchecked)]  (offset body-relative)
//   section bodies: [record_count u32][records...]; every integer field u32
//   except imm_min/imm_max (i64) and tune.value (i64); string refs are u32
//   indexes into STRINGS (index 0 is always "").
//
// Format 1.1 adds six optional sections (ISEL, SCHED, UNITS, MODELS, TUNING,
// FUSION). They are written with DOMSYMAS_DIRFLAG_OPTIONAL so that a reader
// which does not know their id can skip them using the directory size.
//
// Compression layering: the `.symas` payload layer uses the format-defined
// compression id (none/zlib/zstd, via domlibs/domarchive). Any outer
// archive/container layer (e.g. Zstandard framing of whole files) is a
// separate concern handled by domlibs/domarchive directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sandstone.h"

namespace domsymas {

// Re-exported constants with C++ types.
inline constexpr uint32_t kMagic = 0xCEEB09u;
inline constexpr uint8_t kMagicBytes[3] = {0xCE, 0xEB, 0x09};
inline constexpr uint16_t kVersion = 1;
inline constexpr uint32_t kDbMagic = 0x53444231u; // "SDB1"
inline constexpr size_t kFileHeaderSize = 23;
inline constexpr size_t kDirEntrySize = 20;
inline constexpr uint32_t kNoParent = 0xFFFFFFFFu;
inline constexpr uint32_t kNoId = 0xFFFFFFFFu;

enum class Compression : uint8_t { None = 0, Zlib = 1, Zstd = 2 };
enum class Section : uint16_t {
    Strings = 1, Target = 2, Registers = 3, Features = 4, Mnemonics = 5,
    Opcodes = 6, Operands = 7, Encoders = 8, Syntax = 9, Sections = 10,
    Linking = 11, Relocations = 12, Index = 13, Debug = 14,
    // Optional optimization sections (format 1.1+).
    Isel = 15, Sched = 16, Units = 17, Models = 18, Tuning = 19, Fusion = 20
};
inline constexpr uint16_t kSectionMax = 20;
// Sections a reader may skip when it does not recognize the identifier.
inline constexpr uint16_t kFirstOptionalSection = 15;

enum class IselKind : uint32_t {
    Node = 0, Pattern = 1, Pseudo = 2, Expansion = 3, Select = 4,
    PostIsel = 5, Copy = 6, Aggressive = 7
};
enum class UnitKind : uint32_t {
    Other = 0, Int = 1, Fp = 2, Load = 3, Store = 4, Branch = 5, Vector = 6,
    Mask = 7, Divider = 8, Mul = 9, Complex = 10, Pipe = 11, System = 12
};
enum class TuneKind : uint32_t {
    Generic = 0, Latency = 1, Uops = 2, Cost = 3, Prefer = 4, Avoid = 5,
    Threshold = 6, Unroll = 7, VecWidth = 8, Pair = 9, Strategy = 10,
    Preroll = 11, Split = 12
};
enum class FusionKind : uint32_t {
    Macro = 0, CmpBranch = 1, BranchPair = 2, MemArith = 3, MulAdd = 4,
    Shift = 5, Mov = 6, LoadOp = 7, CmpSelect = 8, VSetVli = 9, Target = 10
};

// Sentinel for a variadic latency (LLVM's SchedWrite variadic cycle count).
inline constexpr uint32_t kVariadicLatency = 0xFFFFFFFFu;

enum class OperandKind : uint32_t {
    Reg = 0, Imm = 1, Disp = 2, Mem = 3, Abs = 4, Rel = 5, Sym = 6,
    Cond = 7, RegList = 8, Implicit = 9
};

struct FileHeader {
    uint16_t version = kVersion;
    uint16_t flags = DOMSYMAS_FLAG_BODY_RELATIVE | DOMSYMAS_FLAG_SHARED_STRINGS |
                     DOMSYMAS_FLAG_CHECKSUM;
    Compression compression = Compression::Zlib;
    uint32_t uncompressed_size = 0;
    uint32_t compressed_size = 0;
    uint32_t checksum = 0; // crc32 of uncompressed payload
    bool big_endian = false;
};

struct Error {
    DomSymAs_Error code = DOMSYMAS_OK;
    std::string message;
    size_t offset = 0;
    bool ok() const { return code == DOMSYMAS_OK; }
};

// ---- Record structs (C++ side; strings owned as std::string) ----
struct FeatureRec { uint32_t id = 0; std::string name; uint32_t flags = 0; std::string desc; };
struct RegisterRec {
    uint32_t id = 0; std::string name; uint32_t regclass = 0; uint32_t width = 0;
    uint32_t enc_num = 0; uint32_t parent = kNoParent; uint32_t sub_lo = 0;
    uint32_t sub_hi = 0; uint32_t flags = 0;
};
struct TargetRec {
    uint32_t id = 0; std::string arch, subarch; uint32_t addr_size = 0;
    uint32_t default_op_size = 0, default_addr_size = 0, instr_align = 1;
    uint32_t byte_order = 0; std::string obj_format, abi, calling_conv;
    std::vector<uint32_t> features;
};
struct MnemonicRec {
    uint32_t id = 0; std::string canonical; std::vector<std::string> aliases;
    uint32_t flags = 0; std::vector<uint32_t> forms;
};
struct FormRec {
    uint32_t id = 0, mnemonic = 0, operand_count = 0, flags = 0, encoder = 0;
    std::vector<uint32_t> required, forbidden, syntaxes;
};
struct OperandRec {
    uint32_t id = 0, form = 0, position = 0, kind = 0, width = 0, regclass = 0;
    int64_t imm_min = 0, imm_max = 0; uint32_t is_signed = 0, enc_role = 0;
    uint32_t access = 0, hidden = 0; std::string constraint;
};
struct EncoderRec {
    uint32_t id = 0; std::string name, source; uint32_t flags = 0;
    uint32_t max_bytes = 64, max_relocs = 8;
};
struct SyntaxRec {
    uint32_t id = 0; std::string name; uint32_t operand_order = 0;
    std::string reg_prefix, imm_prefix, comment, separator, tmpl;
    uint32_t case_sensitive = 0; std::vector<uint32_t> forms;
};
struct SectionRec {
    uint32_t id = 0; std::string name; uint32_t flags = 0, align = 1;
    uint32_t addrspace = 0, content = 0;
};
struct LinkageRec {
    uint32_t id = 0; std::string name; uint32_t binding = 0, visibility = 0;
    uint32_t symtype = 0, flags = 0;
};
struct RelocRec {
    uint32_t id = 0; std::string name; uint32_t width = 0, offset = 0;
    uint32_t is_signed = 0, pcrel = 0, overflow = 0, addend = 0; std::string expr;
};
struct IndexRec { uint32_t id = 0, kind = 0; std::string key; uint32_t record = 0; };
struct DebugRec { uint32_t id = 0, ref_kind = 0, ref_id = 0; std::string msg; };

// ---- Optimization records (format 1.1+) ----

// ISEL: "given this target operation under these conditions, use this form".
struct IselRec {
    uint32_t id = 0, kind = 0, form = kNoId;
    std::string node;                              // target op / pseudo name
    uint32_t cost = 0, complexity = 0, priority = 0, flags = 0;
    std::vector<uint32_t> required, forbidden, alts;
    std::string pattern;                           // optional source pattern
};

// SCHED: machine-level cost of one instruction form.
struct SchedRec {
    uint32_t id = 0, form = kNoId;
    std::string name;                              // schedule-class name
    uint32_t latency = 0, uops = 1, cycles = 1, flags = 0;
    std::vector<uint32_t> writes, reads;           // unit ids
};

// UNITS: a functional unit / execution port, optionally model-specific.
struct UnitRec {
    uint32_t id = 0, model = kNoId;
    std::string name;
    uint32_t kind = 0, count = 1, latency = 0, throughput = 1, flags = 0;
};

// MODELS: a per-model (subtarget) machine model.
struct ModelRec {
    uint32_t id = 0;
    std::string name, vendor, desc;
    uint32_t issue_width = 1, micro_op_buffer = 0, loop_buffer = 0;
    uint32_t load_latency = 0, mispredict_penalty = 0;
    uint32_t high_latency = 0, high_uops = 0, flags = 0;
    std::vector<uint32_t> features, tune_features;
};

// TUNING: one per-model optimization hint. `kind` selects the meaning of
// `value`; `form`/`form2` scope it to one or two instruction forms.
struct TuneRec {
    uint32_t id = 0, model = kNoId, kind = 0;
    std::string name, desc;
    uint32_t form = kNoId, form2 = kNoId;
    int64_t value = 0;
    uint32_t flags = 0;
    std::vector<uint32_t> features;
};

// FUSION: an instruction pair that a model may fuse into one op.
struct FusionRec {
    uint32_t id = 0, a = kNoId, b = kNoId, kind = 0, latency = 0, flags = 0;
    std::string name, pattern;
    std::vector<uint32_t> features;
};

// Index kinds used by Builder::build() (see domsymas_index_lookup).
enum : uint32_t {
    kIdxMnemonic = 0, kIdxRegister = 1, kIdxFeature = 2, kIdxSyntax = 3,
    kIdxReloc = 4, kIdxSection = 5, kIdxModel = 6, kIdxIselNode = 7,
    kIdxUnit = 8, kIdxSched = 9, kIdxFusion = 10
};

// Decoded databank contents.
struct DatabankData {
    FileHeader file_header;
    bool big_endian = false;
    std::vector<std::string> strings; // [0] == ""
    std::vector<TargetRec> targets;
    std::vector<FeatureRec> features;
    std::vector<RegisterRec> registers;
    std::vector<MnemonicRec> mnemonics;
    std::vector<FormRec> forms;
    std::vector<OperandRec> operands;
    std::vector<EncoderRec> encoders;
    std::vector<SyntaxRec> syntaxes;
    std::vector<SectionRec> sections;
    std::vector<LinkageRec> linkages;
    std::vector<RelocRec> relocs;
    std::vector<IndexRec> index;
    std::vector<DebugRec> debug;
    // Optional optimization sections (format 1.1+). Empty is legal: a bank
    // that carries none simply describes encoding, not optimization.
    std::vector<IselRec> isels;
    std::vector<SchedRec> scheds;
    std::vector<UnitRec> units;
    std::vector<ModelRec> models;
    std::vector<TuneRec> tunes;
    std::vector<FusionRec> fusions;
};

// ---- Encoding language (Algol-like, parsed with DomDSL combinators) ----
namespace encoding {

struct Expr {
    enum class Kind { Num, Str, Var, Call, Neg, Not, Add, Sub, Mul, Div, Mod,
                      Eq, Ne, Lt, Le, Gt, Ge, And, Or } kind = Kind::Num;
    int64_t num = 0;
    std::string text; // Str literal, Var name, or Call callee
    std::vector<Expr> args; // Call args; binary/unary operands (1-2)
};

struct Stmt {
    enum class Kind { Call, Require, If, Block } kind = Kind::Block;
    std::string callee;          // Call
    std::vector<Expr> args;      // Call
    Expr cond;                   // Require / If
    std::vector<Stmt> body, els; // If / Block
};

struct Program { std::vector<Stmt> stmts; };

struct Value {
    bool is_str = false;
    int64_t num = 0;
    std::string str;
};

struct Limits { size_t max_steps = 100000; size_t max_bytes = 256; size_t max_relocs = 32; };

struct RelocOut { uint32_t type = 0; uint32_t offset = 0; int64_t addend = 0; std::string symbol; std::string type_name; };
struct Outcome { std::vector<uint8_t> bytes; std::vector<RelocOut> relocs; };

// Parse with domlibs/domdsl combinatory parser. Empty message = success.
bool parse(const std::string &source, Program &out, std::string &diag);
// Leading literal bytes (for disassembly candidate prefilter). When nstmts
// is non-null it receives the number of leading statements consumed.
std::vector<uint8_t> literal_prefix(const Program &p, size_t *nstmts = nullptr);
// Execute with operand bindings (name -> value).
bool execute(const Program &p, const std::map<std::string, Value> &operands,
             const Limits &lim, Outcome &out, std::string &diag);

} // namespace encoding

// ---- High-level C++ databank ----
class Databank {
  public:
    Databank() = default;
    Error load_file(const std::string &path);
    Error load_memory(const uint8_t *data, size_t len);
    Error save_file(const std::string &path) const;
    Error to_bytes(std::vector<uint8_t> &out) const;
    Error validate(bool semantic, std::string *diag = nullptr) const;

    const DatabankData &data() const { return data_; }
    DatabankData &mutable_data() { return data_; }
    const std::string &diag() const { return diag_; }

    // Queries (instr.cpp / syntax.cpp / linking.cpp / relocation.cpp).
    std::vector<uint32_t> forms_for_mnemonic(const std::string &name) const;
    const MnemonicRec *find_mnemonic(const std::string &name) const;
    const FormRec *find_form(uint32_t id) const;
    const RegisterRec *find_register(const std::string &name) const;
    const FeatureRec *find_feature(const std::string &name) const;
    const SyntaxRec *find_syntax(const std::string &name) const;
    const SectionRec *find_section(const std::string &name) const;
    const RelocRec *find_reloc(const std::string &name) const;
    const EncoderRec *find_encoder(uint32_t id) const;
    std::vector<const OperandRec *> operands_for_form(uint32_t form) const;
    bool index_lookup(uint32_t kind, const std::string &key, uint32_t &record) const;
    std::vector<uint32_t> resolve(const std::string &mnemonic, uint32_t op_count,
                                  const std::vector<uint32_t> &enabled) const;

    struct EncodeResult { std::vector<uint8_t> bytes; std::vector<encoding::RelocOut> relocs; };
    // Same shape, re-exported for API stability.
    struct RelocOut : encoding::RelocOut {};
    // operands: name -> value (int or symbol string).
    Error encode(uint32_t form, const std::map<std::string, encoding::Value> &ops,
                 EncodeResult &out) const;

    struct DisCandidate { uint32_t form = 0; std::string mnemonic; uint32_t consumed = 0; std::string text; };
    Error disassemble(const uint8_t *code, size_t len, uint32_t syntax,
                      std::vector<DisCandidate> &out) const;
    std::string format_form(const FormRec &f, uint32_t syntax,
                            const std::map<std::string, std::string> &op_text) const;

    // Relocation helpers.
    bool reloc_fits(const RelocRec &r, int64_t value, std::string *diag = nullptr) const;

    // ---- Instruction selection (tuning.cpp) ----
    const ModelRec *find_model(const std::string &name) const;
    uint32_t default_model() const; // DOMSYMAS_MF_DEFAULT, else first record
    const IselRec *find_isel(const std::string &node) const;
    std::vector<const IselRec *> isel_for_node(const std::string &node) const;
    // Candidates for `node`, best first, filtered by `enabled` features and
    // narrowed to `model` when it is not kNoId.
    std::vector<const IselRec *> select_candidates(const std::string &node,
                                                  const std::vector<uint32_t> &enabled,
                                                  uint32_t model) const;
    // Best applicable form id, or kNoId when nothing applies.
    uint32_t select_form(const std::string &node,
                         const std::vector<uint32_t> &enabled,
                         uint32_t model = kNoId) const;

    // ---- Scheduling ----
    const SchedRec *find_sched(uint32_t form) const;
    std::vector<const UnitRec *> units_for_model(uint32_t model) const;
    const UnitRec *find_unit(const std::string &name) const;
    // Base record for `form` with the model's TUNING overrides applied.
    // Returns false when the bank has no base record and no override.
    struct ScheduleInfo {
        bool found = false;
        uint32_t sched = kNoId;
        std::string name;
        uint32_t latency = 0, uops = 1, cycles = 1, flags = 0;
        std::vector<uint32_t> writes, reads;
        uint32_t cost = 0;
        bool cost_known = false;
    };
    ScheduleInfo schedule_for_form(uint32_t form, uint32_t model = kNoId) const;

    // ---- Per-model optimization hints ----
    const TuneRec *find_tune(uint32_t model, uint32_t form, uint32_t kind) const;
    std::vector<const TuneRec *> tunes_for_model(uint32_t model) const;
    std::vector<const TuneRec *> tunes_for_form(uint32_t model, uint32_t form) const;
    // Latest matching override wins; returns false when no hint applies.
    bool tune_value(uint32_t model, uint32_t form, uint32_t kind, int64_t &out) const;

    // ---- Instruction-pair fusion ----
    const FusionRec *find_fusion(uint32_t a, uint32_t b, bool bidirect = true) const;
    std::vector<const FusionRec *> fusion_candidates(uint32_t form) const;

  private:
    DatabankData data_;
    std::string diag_;
};

// ---- Builder (producer side) ----
class Builder {
  public:
    explicit Builder(bool little_endian = true);
    void set_compression(Compression c) { compression_ = c; }
    uint32_t add_target(TargetRec t);
    uint32_t add_feature(const std::string &name, uint32_t flags, const std::string &desc);
    uint32_t add_register(RegisterRec r);
    uint32_t add_mnemonic(const std::string &canonical,
                          const std::vector<std::string> &aliases, uint32_t flags);
    uint32_t add_form(uint32_t mnemonic, uint32_t op_count, uint32_t flags,
                      uint32_t encoder, const std::vector<uint32_t> &req,
                      const std::vector<uint32_t> &forb, const std::vector<uint32_t> &syn);
    uint32_t add_operand(OperandRec o);
    uint32_t add_encoder(const std::string &name, const std::string &source,
                         uint32_t flags, uint32_t max_bytes, uint32_t max_relocs);
    uint32_t add_syntax(SyntaxRec s);
    void link_syntax_form(uint32_t syntax, uint32_t form);
    void link_mnemonic_form(uint32_t mnemonic, uint32_t form);
    uint32_t add_section(const std::string &name, uint32_t flags, uint32_t align,
                         uint32_t addrspace, uint32_t content);
    uint32_t add_linkage(const std::string &name, uint32_t binding,
                         uint32_t visibility, uint32_t symtype, uint32_t flags);
    uint32_t add_reloc(const std::string &name, uint32_t width, uint32_t offset,
                       uint32_t is_signed, uint32_t pcrel, uint32_t overflow,
                       uint32_t addend, const std::string &expr);
    uint32_t add_debug(const std::string &msg, uint32_t ref_kind, uint32_t ref_id);
    // Optimization sections (format 1.1+). Record ids equal the list index.
    uint32_t add_isel(IselRec r);
    uint32_t add_sched(SchedRec r);
    uint32_t add_unit(UnitRec r);
    uint32_t add_model(ModelRec r);
    uint32_t add_tune(TuneRec r);
    uint32_t add_fusion(FusionRec r);
    Error build(Databank &out) const;
    Error save_file(const std::string &path) const;
    // Records added so far. Producers that merge several documents into one
    // builder need these to translate per-document list indices into the
    // record ids the builder actually assigns.
    const DatabankData &data() const { return data_; }

  private:
    bool little_endian_;
    Compression compression_ = Compression::Zlib;
    DatabankData data_;
};

// Free helpers (binary.cpp / container.cpp / compression.cpp / format.cpp).
uint32_t crc32(const uint8_t *data, size_t len);
const char *error_string(DomSymAs_Error e);
const char *section_name(Section s);
bool section_name_to_id(const std::string &name, Section &out);
bool read_file_header(const uint8_t *data, size_t len, FileHeader &out, Error &err);
void write_file_header(const FileHeader &h, std::vector<uint8_t> &out);
Error compress_payload(const std::vector<uint8_t> &in, Compression c,
                       std::vector<uint8_t> &out);
Error decompress_payload(const uint8_t *data, size_t len, Compression c,
                         size_t expected, std::vector<uint8_t> &out);

} // namespace domsymas
