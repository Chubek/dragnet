// DomSymAs end-to-end check: build -> save -> reload -> query -> encode,
// plus the optional optimization sections (ISEL/SCHED/UNITS/MODELS/TUNING/
// FUSION) round trip, queries and C ABI.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>

#include "sandstone.h"
#include "sandstone.hpp"

#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

static int optimization_checks();

int main() {
    using namespace domsymas;
    // ---- C++ builder ----
    Builder b(true);
    uint32_t feat64 = b.add_feature("x86-64", 0, "64-bit mode");
    uint32_t featVex = b.add_feature("avx", 0, "vector extensions");
    TargetRec t;
    t.arch = "x86-64"; t.subarch = ""; t.addr_size = 64; t.default_op_size = 32;
    t.default_addr_size = 64; t.instr_align = 1; t.byte_order = 0;
    t.obj_format = "ELF"; t.abi = "SysV"; t.calling_conv = "sysv";
    t.features = {feat64};
    b.add_target(std::move(t));
    RegisterRec rax; rax.name = "rax"; rax.regclass = 0; rax.width = 64;
    rax.enc_num = 0; rax.flags = 3;
    b.add_register(rax);
    RegisterRec eax; eax.name = "eax"; eax.regclass = 0; eax.width = 32;
    eax.enc_num = 0; eax.parent = 0; eax.sub_lo = 0; eax.sub_hi = 32; eax.flags = 3;
    b.add_register(eax);
    uint32_t mov = b.add_mnemonic("mov", {"MOV"}, 1); // case-insensitive
    uint32_t nop = b.add_mnemonic("nop", {}, 0);
    uint32_t callmn = b.add_mnemonic("call", {}, 0);
    uint32_t enc_mov = b.add_encoder("mov_r64",
        "emit8(0x48); emit8(0x89); emit_modrm(dst, src);", 0, 16, 0);
    uint32_t enc_nop = b.add_encoder("nop_enc", "emit8(0x90);", 0, 16, 0);
    uint32_t enc_call = b.add_encoder("call_rel",
        "emit8(0xE8); emit_disp(4, target); emit_relocation(sym, REL_PC32);", 0, 16, 4);
    SyntaxRec intel; intel.name = "intel"; intel.separator = ", ";
    intel.tmpl = "{mnemonic} {ops}";
    uint32_t syn_intel = b.add_syntax(std::move(intel));
    SyntaxRec att; att.name = "att"; att.reg_prefix = "%"; att.imm_prefix = "$";
    att.operand_order = 1; att.tmpl = "{mnemonic} {ops}";
    uint32_t syn_att = b.add_syntax(std::move(att));
    uint32_t f_mov = b.add_form(mov, 2, DOMSYMAS_IF_READS_MEM | DOMSYMAS_IF_WRITES_MEM,
                                enc_mov, {feat64}, {}, {});
    uint32_t f_nop = b.add_form(nop, 0, 0, enc_nop, {}, {}, {});
    uint32_t f_call = b.add_form(callmn, 1,
                                 DOMSYMAS_IF_BRANCH, enc_call, {feat64}, {featVex}, {});
    OperandRec o0; o0.form = f_mov; o0.position = 0; o0.kind = 0; o0.width = 64;
    b.add_operand(o0);
    OperandRec o1; o1.form = f_mov; o1.position = 1; o1.kind = 0; o1.width = 64;
    b.add_operand(o1);
    OperandRec oc; oc.form = f_call; oc.position = 0; oc.kind = 6; oc.width = 64;
    b.add_operand(oc);
    b.link_mnemonic_form(mov, f_mov);
    b.link_mnemonic_form(nop, f_nop);
    b.link_mnemonic_form(callmn, f_call);
    b.link_syntax_form(syn_intel, f_mov);
    b.link_syntax_form(syn_att, f_mov);
    b.add_section(".text", DOMSYMAS_SF_EXEC | DOMSYMAS_SF_ALLOC | DOMSYMAS_SF_CODE, 16, 0, 1);
    b.add_linkage("global_func", DOMSYMAS_BIND_GLOBAL, DOMSYMAS_VIS_DEFAULT, 2, 0);
    b.add_reloc("REL_PC32", 32, 0, 1, 1, 0, DOMSYMAS_ADDEND_EXPLICIT, "S + A - P");
    b.add_reloc("REL_ABS32", 32, 0, 0, 0, 0, DOMSYMAS_ADDEND_EXPLICIT, "S + A");
    b.add_debug("mov r64 encoding", 6, f_mov);


    Databank bank;
    Error e = b.build(bank);
    CHECK(e.ok(), "build: " + e.message);
    // Empty-string stability + index auto-build
    CHECK(!bank.data().strings.empty() && bank.data().strings[0].empty(), "strings[0]");
    uint32_t rec = 0;
    CHECK(bank.index_lookup(0, "mov", rec), "index mov");

    const char *path = "/tmp/sandstone-test.symas";
    e = bank.save_file(path);
    CHECK(e.ok(), "save: " + e.message);

    // ---- reload + validate ----
    Databank bank2;
    e = bank2.load_file(path);
    CHECK(e.ok(), "load: " + e.message);
    std::string dg;
    e = bank2.validate(true, &dg);
    CHECK(e.ok(), "validate: " + dg);

    // ---- queries ----
    CHECK(bank2.find_mnemonic("MOV") != nullptr, "case-insensitive lookup");
    CHECK(bank2.forms_for_mnemonic("mov").size() == 1, "forms_for_mnemonic");
    CHECK(bank2.find_register("eax")->parent == 0, "subregister parent");
    CHECK(bank2.find_feature("avx") != nullptr, "feature");
    CHECK(bank2.find_syntax("att")->operand_order == 1, "att reversed");
    CHECK(bank2.find_section(".text") != nullptr, "section");
    CHECK(bank2.find_reloc("REL_PC32")->pcrel == 1, "reloc");
    auto ids = bank2.resolve("mov", 2, {feat64});
    CHECK(ids.size() == 1 && ids[0] == f_mov, "resolve with feature");
    CHECK(bank2.resolve("call", 1, {}).empty(), "resolve missing required feature");
    CHECK(bank2.resolve("call", 1, {feat64}).size() == 1, "resolve call ok");
    CHECK(bank2.resolve("call", 1, {feat64, featVex}).empty(), "resolve forbidden feature");

    // ---- encode ----
    Databank::EncodeResult er;
    std::map<std::string, encoding::Value> ops;
    encoding::Value d, s;
    d.num = 0; s.num = 3; // rax, rbx
    ops["dst"] = d; ops["src"] = s;
    e = bank2.encode(f_mov, ops, er);
    CHECK(e.ok(), "encode: " + e.message);
    CHECK(er.bytes.size() == 3 && er.bytes[0] == 0x48 && er.bytes[1] == 0x89 &&
          er.bytes[2] == (0xC0 | (0 << 3) | 3), "encode bytes");

    std::map<std::string, encoding::Value> cops;
    encoding::Value tv, sy;
    tv.num = 0x12345678; sy.is_str = true; sy.str = "puts";
    cops["target"] = tv; cops["sym"] = sy;
    e = bank2.encode(f_call, cops, er);
    CHECK(e.ok(), "encode call: " + e.message);
    CHECK(er.bytes.size() == 5 && er.bytes[0] == 0xE8, "call bytes");
    CHECK(er.relocs.size() == 1 && er.relocs[0].symbol == "puts", "call reloc");

    // bad operand -> controlled encode error, no partial output reliance
    std::map<std::string, encoding::Value> bad;
    bad["dst"] = d;
    e = bank2.encode(f_mov, bad, er);
    CHECK(!e.ok() && e.code == DOMSYMAS_ERR_ENCODE, "encode missing operand errors");

    // ---- disassemble ----
    std::vector<Databank::DisCandidate> cands;
    uint8_t code[] = {0x48, 0x89, 0xD8, 0x90};
    e = bank2.disassemble(code, 3, syn_intel, cands);
    CHECK(e.ok() && !cands.empty(), "disassemble");
    CHECK(cands[0].text == "mov reg3, rm0(mod=3)", "disasm text: " + cands[0].text);
    e = bank2.disassemble(code + 3, 1, syn_intel, cands);
    CHECK(e.ok() && cands.size() == 1 && cands[0].text == "nop ", "disasm nop: " + cands[0].text);

    // ---- reloc overflow ----
    const RelocRec *rpc = bank2.find_reloc("REL_PC32");
    CHECK(bank2.reloc_fits(*rpc, 127, nullptr), "fits");
    CHECK(!bank2.reloc_fits(*rpc, (int64_t)0xFFFFFFFFFLL, &dg), "overflow diagnosed");

    // ---- malformed inputs ----
    std::vector<uint8_t> raw;
    e = bank2.to_bytes(raw);
    CHECK(e.ok(), "to_bytes");
    CHECK(raw[0] == 0xCE && raw[1] == 0xEB && raw[2] == 0x09, "magic bytes");
    Databank badb;
    std::vector<uint8_t> corrupted = raw;
    corrupted[0] = 0x00;
    e = badb.load_memory(corrupted.data(), corrupted.size());
    CHECK(e.code == DOMSYMAS_ERR_BAD_MAGIC, "bad magic rejected");
    e = badb.load_memory(raw.data(), 10);
    CHECK(e.code == DOMSYMAS_ERR_TRUNCATED, "truncation rejected");
    corrupted = raw;
    corrupted[kFileHeaderSize + 2] ^= 0xFF;
    e = badb.load_memory(corrupted.data(), corrupted.size());
    CHECK(!e.ok(), "compressed corruption rejected: " + e.message);

    // ---- C ABI ----
    DomSymAs_Bank *cb = nullptr;
    CHECK(domsymas_open_file(path, &cb) == DOMSYMAS_OK, "c open");
    CHECK(domsymas_validate(cb, 1) == DOMSYMAS_OK, "c validate");
    uint32_t forms[8], n = 0;
    CHECK(domsymas_find_mnemonic(cb, "mov", forms, 8, &n) == DOMSYMAS_OK && n == 1, "c find");
    uint32_t res[8]; uint32_t rn = 0;
    uint32_t en[] = {(uint32_t)feat64};
    CHECK(domsymas_resolve(cb, "mov", 2, en, 1, res, 8, &rn) == DOMSYMAS_OK && rn == 1, "c resolve");
    DomSymAs_OpVal cop[2] = {{"dst", 0, 0, nullptr}, {"src", 0, 3, nullptr}};
    DomSymAs_Bytes ob{nullptr, 0};
    DomSymAs_RelocOut *rl = nullptr;
    uint32_t rln = 0;
    CHECK(domsymas_encode(cb, forms[0], cop, 2, &ob, &rl, &rln) == DOMSYMAS_OK, "c encode");
    CHECK(ob.len == 3 && ob.data[2] == 0xC3, "c encode bytes");
    domsymas_free_bytes(&ob);
    domsymas_free_relocs(rl, rln);
    DomSymAs_Candidate *dc = nullptr;
    uint32_t dcn = 0;
    CHECK(domsymas_disassemble(cb, code, 3, 0, &dc, &dcn) == DOMSYMAS_OK && dcn >= 1, "c disasm");
    domsymas_free_candidates(dc, dcn);
    CHECK(domsymas_probe_magic(raw.data(), raw.size()) == 1, "c probe");
    CHECK(domsymas_string_count(cb) > 0 && domsymas_string_at(cb, 0)[0] == '\0', "c strings");
    // out-of-range string ref must not crash
    CHECK(domsymas_string_at(cb, 0xFFFFFF) == nullptr, "c string oob");
    CHECK(domsymas_register_count(cb) == 2, "c registers");
    CHECK(domsymas_find_register(cb, "rax") != nullptr, "c find reg");
    CHECK(domsymas_syntax_count(cb) == 2, "c syntaxes");
    CHECK(domsymas_reloc_count(cb) == 2, "c relocs");
    CHECK(domsymas_find_reloc(cb, "REL_ABS32") != nullptr, "c find reloc");
    uint32_t rid = 0;
    CHECK(domsymas_index_lookup(cb, 0, "mov", &rid) == DOMSYMAS_OK, "c index");
    domsymas_close(cb);

    // ---- big-endian + zstd + uncompressed variants ----
    Builder bb(false);
    bb.set_compression(Compression::Zstd);
    bb.add_feature("f", 0, "");
    uint32_t me = bb.add_encoder("e", "emit8(1);", 0, 8, 0);
    uint32_t mm = bb.add_mnemonic("m", {}, 0);
    uint32_t ff = bb.add_form(mm, 0, 0, me, {}, {}, {});
    bb.link_mnemonic_form(mm, ff);
    Databank bbe;
    CHECK(bb.build(bbe).ok(), "be build");
    std::vector<uint8_t> beraw;
    CHECK(bbe.to_bytes(beraw).ok(), "be bytes");
    Databank bbe2;
    CHECK(bbe2.load_memory(beraw.data(), beraw.size()).ok(), "be load");
    CHECK(bbe2.validate(true, &dg).ok(), "be validate: " + dg);

    Builder bn(true);
    bn.set_compression(Compression::None);
    uint32_t ne = bn.add_encoder("e", "emit8(2);", 0, 8, 0);
    uint32_t nm = bn.add_mnemonic("m", {}, 0);
    uint32_t nf = bn.add_form(nm, 0, 0, ne, {}, {}, {});
    bn.link_mnemonic_form(nm, nf);
    Databank bnn;
    CHECK(bn.build(bnn).ok(), "none build");
    std::vector<uint8_t> nraw;
    CHECK(bnn.to_bytes(nraw).ok(), "none bytes");
    Databank bnn2;
    CHECK(bnn2.load_memory(nraw.data(), nraw.size()).ok(), "none load");

    std::cout << "ALL DOMSYMAS CHECKS PASSED\n";
    return optimization_checks();
}

// ---- Optimization sections (ISEL / SCHED / UNITS / MODELS / TUNING / FUSION)
//
// The bank built here mirrors what a stdspec produces: a generic model and a
// wide model, per-model execution units, one selection record per alternative
// for a node, a scheduling record per form, per-model latency/cost/preference
// overrides, and one fusion pair.
static int optimization_checks() {
    int failures = 0;
#define OPT(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; failures++; } } while (0)
    using namespace domsymas;

    Builder b(true);
    uint32_t fExtA = b.add_feature("exta", 0, "");
    uint32_t fBase = b.add_feature("base", 0, "");
    TargetRec t;
    t.arch = "test"; t.addr_size = 64; t.default_op_size = 32; t.default_addr_size = 64;
    t.byte_order = 0; t.obj_format = "ELF64"; t.features = {fBase};
    b.add_target(std::move(t));
    uint32_t mAdd = b.add_mnemonic("add", {}, 0);
    uint32_t mAddV = b.add_mnemonic("addv", {}, 0);
    uint32_t mLd = b.add_mnemonic("ld", {}, 0);
    uint32_t mBr = b.add_mnemonic("beq", {}, 0);
    uint32_t fAdd = b.add_form(mAdd, 2, 0, b.add_encoder("add_rr", "emit32le(0x33);", 0, 4, 0), {}, {}, {});
    uint32_t fAddV = b.add_form(mAddV, 2, 0, b.add_encoder("addv_rr", "emit32le(0x57);", 0, 4, 0),
                                {fExtA}, {}, {});
    uint32_t fLd = b.add_form(mLd, 1, DOMSYMAS_IF_READS_MEM, b.add_encoder("ld", "emit32le(0x03);", 0, 4, 0), {}, {}, {});
    uint32_t fBr = b.add_form(mBr, 2, DOMSYMAS_IF_COND_BRANCH, b.add_encoder("beq", "emit32le(0x63);", 0, 4, 0), {}, {}, {});
    b.link_mnemonic_form(mAdd, fAdd);
    b.link_mnemonic_form(mAddV, fAddV);
    b.link_mnemonic_form(mLd, fLd);
    b.link_mnemonic_form(mBr, fBr);
    for (uint32_t form : {fAdd, fAddV, fBr}) {
        for (uint32_t pos = 0; pos < 2; pos++) {
            OperandRec o;
            o.form = form; o.position = pos; o.kind = DOMSYMAS_OP_REG; o.width = 64;
            o.access = pos == 0 ? DOMSYMAS_ACC_WRITE : DOMSYMAS_ACC_READ;
            b.add_operand(std::move(o));
        }
    }
    {
        OperandRec o;
        o.form = fLd; o.position = 0; o.kind = DOMSYMAS_OP_MEM; o.width = 64;
        o.access = DOMSYMAS_ACC_WRITE;
        b.add_operand(std::move(o));
    }

    ModelRec gen;
    gen.name = "generic"; gen.issue_width = 4; gen.micro_op_buffer = 32;
    gen.load_latency = 4; gen.mispredict_penalty = 10;
    gen.flags = DOMSYMAS_MF_OUT_OF_ORDER | DOMSYMAS_MF_DEFAULT;
    gen.features = {fBase};
    uint32_t mGen = b.add_model(std::move(gen));
    ModelRec wide;
    wide.name = "wide"; wide.issue_width = 6; wide.micro_op_buffer = 224;
    wide.load_latency = 5; wide.mispredict_penalty = 14; wide.loop_buffer = 50;
    wide.features = {fBase, fExtA};
    uint32_t mWide = b.add_model(std::move(wide));

    UnitRec ialu; ialu.name = "ialu"; ialu.kind = (uint32_t)UnitKind::Int; ialu.count = 4;
    ialu.latency = 1; ialu.throughput = 1; ialu.flags = DOMSYMAS_UF_PIPELINED;
    uint32_t uIalu = b.add_unit(std::move(ialu));
    UnitRec fpu; fpu.name = "fpu"; fpu.model = mWide; fpu.kind = (uint32_t)UnitKind::Fp;
    fpu.count = 2; fpu.latency = 3; fpu.throughput = 1;
    uint32_t uFpu = b.add_unit(std::move(fpu));

    IselRec i0; i0.node = "add"; i0.form = fAdd; i0.cost = 1; i0.complexity = 1;
    i0.flags = DOMSYMAS_IF_COMMUTES;
    b.add_isel(std::move(i0));
    IselRec i1; i1.node = "add"; i1.form = fAddV; i1.cost = 1; i1.complexity = 5;
    i1.required = {fExtA}; i1.pattern = "(add GPR:$a, (i64 0))";
    b.add_isel(std::move(i1));
    IselRec i2; i2.node = "MULHU"; i2.kind = (uint32_t)IselKind::Pseudo; i2.form = fAddV;
    i2.cost = 4; i2.flags = DOMSYMAS_IF_EXPAND;
    b.add_isel(std::move(i2));

    SchedRec s0; s0.form = fAdd; s0.name = "WriteIALU"; s0.latency = 1; s0.uops = 1;
    s0.writes = {uIalu}; s0.reads = {uIalu};
    b.add_sched(std::move(s0));
    SchedRec s1; s1.form = fAddV; s1.name = "WriteFPU"; s1.latency = kVariadicLatency;
    s1.uops = 1; s1.flags = DOMSYMAS_SCH_VARIADIC; s1.writes = {uFpu}; s1.reads = {uFpu};
    b.add_sched(std::move(s1));
    SchedRec s2; s2.form = fBr; s2.name = "WriteBr"; s2.latency = 1; s2.uops = 1;
    s2.flags = DOMSYMAS_SCH_FUSED;
    b.add_sched(std::move(s2));

    TuneRec t0; t0.model = mWide; t0.kind = (uint32_t)TuneKind::Latency; t0.name = "latency";
    t0.form = fAddV; t0.value = 3; t0.flags = DOMSYMAS_TF_OVERRIDE;
    b.add_tune(std::move(t0));
    TuneRec t1; t1.model = mWide; t1.kind = (uint32_t)TuneKind::Prefer; t1.name = "prefer";
    t1.form = fAddV; t1.value = 8;
    b.add_tune(std::move(t1));
    TuneRec t2; t2.model = mGen; t2.kind = (uint32_t)TuneKind::Avoid; t2.name = "avoid";
    t2.form = fAddV; t2.value = 1;
    b.add_tune(std::move(t2));
    TuneRec t3; t3.model = mWide; t3.kind = (uint32_t)TuneKind::Cost; t3.name = "cost";
    t3.form = fAdd; t3.value = 7;
    b.add_tune(std::move(t3));

    FusionRec fu; fu.name = "cmp-branch"; fu.a = fBr; fu.b = fAdd;
    fu.kind = (uint32_t)FusionKind::CmpBranch;
    fu.flags = DOMSYMAS_FF_PROFITABLE | DOMSYMAS_FF_REQUIRES_FLAGS | DOMSYMAS_FF_BIDIRECTIONAL;
    b.add_fusion(std::move(fu));

    Databank bank;
    Error e = b.build(bank);
    OPT(e.ok(), "optimization build: " + e.message);
    if (!e.ok()) return 1;
    const char *path = "/tmp/sandstone-smoke-opt.symas";
    e = bank.save_file(path);
    OPT(e.ok(), "optimization save: " + e.message);
    Databank r;
    e = r.load_file(path);
    OPT(e.ok(), "optimization load: " + e.message);
    std::string dg;
    e = r.validate(true, &dg);
    OPT(e.ok(), "optimization validate: " + dg);
    if (!e.ok()) return 1;

    OPT(r.data().isels.size() == 3 && r.data().scheds.size() == 3 && r.data().units.size() == 2 &&
            r.data().models.size() == 2 && r.data().tunes.size() == 4 && r.data().fusions.size() == 1,
        "record counts survive the round trip");
    OPT(r.find_model("wide")->micro_op_buffer == 224 && r.find_model("wide")->loop_buffer == 50,
        "model fields");
    OPT(r.default_model() == mGen, "default model");
    // Selection: the alternative with the higher complexity wins once the
    // feature is enabled, and the generic model avoids it entirely.
    OPT(r.select_form("add", {}) == fAdd, "select add without the feature");
    OPT(r.select_form("add", {fExtA}) == fAddV, "select add with the feature");
    OPT(r.select_form("add", {fExtA}, mGen) == fAdd, "generic model avoids the extension");
    OPT(r.select_form("add", {fExtA}, mWide) == fAddV, "wide model prefers the extension");
    OPT(r.select_form("nosuchnode", {}) == kNoId, "unknown node selects nothing");
    OPT(r.isel_for_node("add").size() == 2, "alternatives");
    OPT(r.find_isel("MULHU")->kind == (uint32_t)IselKind::Pseudo, "pseudo record kind");
    // Scheduling: base record, then the model override, then the model fallback.
    const SchedRec *s = r.find_sched(fAdd);
    OPT(s && s->latency == 1 && s->writes.size() == 1 && s->writes[0] == uIalu, "base schedule");
    OPT(r.schedule_for_form(fAddV, mWide).latency == 3, "model latency override");
    OPT(r.schedule_for_form(fAddV, mGen).latency == kVariadicLatency, "base latency kept");
    OPT(r.schedule_for_form(fAdd, mWide).cost == 7, "model cost override");
    OPT(!r.schedule_for_form(fAdd, kNoId).cost_known, "no model means no cost override");
    OPT(r.schedule_for_form(fLd, mWide).latency == 5, "memory form falls back to the load latency");
    OPT(r.find_sched(fBr)->flags & DOMSYMAS_SCH_FUSED, "fused flag");
    OPT(r.units_for_model(mWide).size() == 2, "target-wide plus model units");
    // Tuning and fusion.
    int64_t v = 0;
    OPT(r.tune_value(mWide, fAddV, (uint32_t)TuneKind::Latency, v) && v == 3, "tune lookup");
    OPT(!r.tune_value(mGen, fAddV, (uint32_t)TuneKind::Latency, v), "tune miss");
    OPT(r.find_fusion(fBr, fAdd) != nullptr, "fusion lookup");
    OPT(r.find_fusion(fAdd, fBr) != nullptr, "bidirectional fusion lookup");
    OPT(r.find_fusion(fAdd, fAddV, false) == nullptr, "no invented fusion");
    // Index kinds.
    uint32_t rec = 0;
    OPT(r.index_lookup(kIdxModel, "wide", rec) && rec == mWide, "index model");
    OPT(r.index_lookup(kIdxIselNode, "add", rec), "index selection node");
    OPT(r.index_lookup(kIdxUnit, "ialu", rec) && rec == uIalu, "index unit");
    OPT(r.index_lookup(kIdxSched, "WriteIALU", rec), "index schedule class");
    OPT(r.index_lookup(kIdxFusion, "cmp-branch", rec), "index fusion");
    // C ABI.
    DomSymAs_Bank *cb = nullptr;
    OPT(domsymas_open_file(path, &cb) == DOMSYMAS_OK, "c open");
    OPT(domsymas_model_count(cb) == 2 && domsymas_isel_count(cb) == 3 &&
            domsymas_sched_count(cb) == 3 && domsymas_unit_count(cb) == 2 &&
            domsymas_tune_count(cb) == 4 && domsymas_fusion_count(cb) == 1, "c counts");
    const DomSymAs_Model *cm = domsymas_find_model(cb, "wide");
    OPT(cm && cm->issue_width == 6 && cm->feature_count == 2, "c model");
    OPT(domsymas_default_model(cb) == mGen, "c default model");
    uint32_t en[] = {fExtA};
    uint32_t sel = 0;
    OPT(domsymas_select_form(cb, "add", en, 1, kNoId, &sel) == DOMSYMAS_OK && sel == fAddV,
        "c select");
    DomSymAs_SchedInfo si;
    OPT(domsymas_sched_info(cb, fAddV, mWide, &si) == DOMSYMAS_OK && si.found && si.latency == 3,
        "c schedule info");
    domsymas_free_sched_info(&si);
    int64_t cv = 0;
    OPT(domsymas_tune_value(cb, mWide, fAddV, (uint32_t)TuneKind::Latency, &cv) == DOMSYMAS_OK && cv == 3,
        "c tune value");
    OPT(domsymas_find_fusion(cb, fBr, fAdd, 1) != nullptr, "c fusion");
    domsymas_close(cb);
    // Dangling references are rejected.
    {
        Builder bad(true);
        bad.add_feature("base", 0, "");
        uint32_t mm = bad.add_mnemonic("m", {}, 0);
        uint32_t ff = bad.add_form(mm, 0, 0, bad.add_encoder("e", "emit8(1);", 0, 4, 0), {}, {}, {});
        bad.link_mnemonic_form(mm, ff);
        IselRec v2; v2.node = "bogus"; v2.form = ff + 42;
        bad.add_isel(std::move(v2));
        Databank db;
        OPT(!bad.build(db).ok(), "dangling selection reference rejected");
    }
    {
        Builder bad(true);
        bad.add_feature("base", 0, "");
        uint32_t mm = bad.add_mnemonic("m", {}, 0);
        uint32_t ff = bad.add_form(mm, 0, 0, bad.add_encoder("e", "emit8(1);", 0, 4, 0), {}, {}, {});
        bad.link_mnemonic_form(mm, ff);
        SchedRec a1; a1.form = ff; bad.add_sched(std::move(a1));
        SchedRec a2; a2.form = ff; bad.add_sched(std::move(a2));
        Databank db;
        OPT(!bad.build(db).ok(), "duplicate scheduling record rejected");
    }
    // A bank with no optimization sections is still valid and reports nothing.
    {
        Builder plain(true);
        plain.add_feature("base", 0, "");
        uint32_t mm = plain.add_mnemonic("m", {}, 0);
        uint32_t ff = plain.add_form(mm, 0, 0, plain.add_encoder("e", "emit8(1);", 0, 4, 0), {}, {}, {});
        plain.link_mnemonic_form(mm, ff);
        Databank db;
        OPT(plain.build(db).ok(), "plain bank builds");
        std::vector<uint8_t> raw;
        OPT(db.to_bytes(raw).ok(), "plain bank bytes");
        Databank db2;
        OPT(db2.load_memory(raw.data(), raw.size()).ok(), "plain bank reload");
        OPT(db2.validate(true, &dg).ok(), "plain bank validate: " + dg);
        OPT(db2.find_sched(ff) == nullptr, "plain bank has no schedule record");
        OPT(db2.default_model() == kNoId, "plain bank has no model");
        OPT(db2.select_form("m", {}, kNoId) == kNoId, "plain bank selects nothing");
    }
    if (failures == 0) std::cout << "ALL OPTIMIZATION-SECTION CHECKS PASSED\n";
    return failures ? 1 : 0;
#undef OPT
}

