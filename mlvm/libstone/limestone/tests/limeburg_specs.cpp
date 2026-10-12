#include "test.hpp"
#include "limeburg/infobank.hpp"
#include "metacode/json.hpp"
#include <filesystem>
#include <fstream>
#include <set>

using namespace limestone;
using namespace limestone::limeburg;
static std::string read(const std::filesystem::path& path) {std::ifstream file(path);CHECK(file.good());return {(std::istreambuf_iterator<char>(file)),{}};}
static Node input(uint32_t id,std::string klass="GPR") {Node node{id,"INPUT","",{}};node.required=false;node.register_class=std::move(klass);return node;}
static Node operation(uint32_t id,std::string op,std::vector<uint32_t> children,std::string klass="GPR") {Node node{id,std::move(op),"",std::move(children)};node.register_class=std::move(klass);return node;}
static metacode::Architecture fixture() {return take(metacode::parse_isa(R"ISA(
arch fixture {}
regclass G { r0(32)=0, r1(32)=1, }
encoding E { width=32; imm=8:4; signed=1; }
op plus {
  operands=rd:G, rs:G, imm:imm; encoding=E; semantics="(set rd (add rs imm))";
  tooling={
    dataflow={uses=["rs","imm"];defs=["rd"];explicit_operands=["rd","rs","imm"];flags_read=false;flags_written=false;memory="none";};
    vmm={terminator=false;control_flow="fallthrough";may_trap=false;atomic=false;serializing=false;};
    instruction_selection={pseudo=false;cost=7;};
  };
}
)ISA","fixture.isa"));}
static metacode::Value::Object& section(metacode::Architecture& architecture,const char* name) {
  return std::get<metacode::Value::Object>(std::get<metacode::Value::Object>(architecture.operations[0].fields.at("tooling").data).at(name).data);
}
int main(int argc,char** argv) {return test_main([&] {
  CHECK(argc==2);const std::filesystem::path root=argv[1];
  auto manifest=take(metacode::parse_json(read(root/"metacode/infobank/manifest.json")));
  const auto& architectures=std::get<metacode::Value::Array>(std::get<metacode::Value::Object>(manifest.data).at("architectures").data);
  size_t inventory_rules=0;
  for(const auto& item:architectures) {
    const auto& entry=std::get<metacode::Value::Object>(item.data);std::filesystem::path source=entry.at("file").text();
    auto architecture=take(metacode::parse_isa(read(root/"metacode/infobank"/source),"metacode/infobank/"+source.generic_string()));
    auto specification=take(from_infobank(architecture));CHECK(specification.coverage.size()==architecture.operations.size());
    auto document=take(load_rules_file((root/"limeburg/specs"/(source.stem().string()+".lburg")).string()));CHECK(document.name==architecture.name);
    CHECK(take(print_rules(document))==take(print_rules(specification.document)));
    CHECK(specification.machine.metadata.contains("compiler")&&specification.machine.metadata.contains("tooling"));
    std::set<std::string> instructions;for(auto& op:architecture.operations)instructions.insert(op.name);
    for(auto& rule:document.rules.rules) {
      if(rule.external_only){CHECK(rule.instruction.empty()&&rule.cost==0);continue;}
      ++inventory_rules;CHECK(instructions.contains(rule.instruction));CHECK(rule.origin.starts_with("metacode/infobank/"));
      auto& record=specification.coverage.at(rule.id-1);CHECK(record.instruction==rule.instruction&&record.rule==rule.id);
    }
    for(auto& record:specification.coverage)if(!record.rule)CHECK(record.mode=="unsupported"&&!record.reason.empty());
  }
  CHECK(architectures.size()>20&&inventory_rules>500);
  auto riscv=take(load_rules_file((root/"limeburg/specs/riscv64.lburg").string()));
  std::vector<Node> add={input(1),input(2),operation(3,"ISA_add",{1,2})};
  auto selected=take(select(add,3,riscv.rules,"value"));CHECK(selected.cost==1);
  auto emitted=take(emit_scheduler(add,riscv.rules,selected));CHECK(emitted.instructions.size()==1);
  CHECK(emitted.instructions[0].opcode=="add"&&emitted.instructions[0].uses==std::vector<uint32_t>({1,2})&&emitted.instructions[0].defs==std::vector<uint32_t>({3}));
  CHECK(emitted.instructions[0].origin.find("riscv64.isa:")!=std::string::npos);
  add[0].register_class="FPR";fails(select(add,3,riscv.rules,"value"),Error::Code::Unsatisfiable);add[0].register_class="GPR";
  add[2].register_class="FPR";fails(select(add,3,riscv.rules,"value"),Error::Code::Unsatisfiable);add[2].register_class="GPR";
  add[2].op="add";fails(select(add,3,riscv.rules,"value"),Error::Code::Unsatisfiable);
  Node constant{2,"CONST","",{}};constant.has_imm=true;constant.imm=2047;
  std::vector<Node> addi={input(1),constant,operation(3,"ISA_addi",{1,2})};
  CHECK(take(select(addi,3,riscv.rules,"value")).cost==1);
  auto immediate=take(emit_scheduler(addi,riscv.rules,take(select(addi,3,riscv.rules,"value"))));
  CHECK(immediate.instructions[0].opcode=="addi"&&immediate.instructions[0].immediates==std::vector<std::pair<uint32_t,int64_t>>({{2,2047}}));
  addi[1].imm=-2048;take(select(addi,3,riscv.rules,"value"));
  for(auto invalid:{int64_t(-2049),int64_t(2048)}){addi[1].imm=invalid;fails(select(addi,3,riscv.rules,"value"),Error::Code::Unsatisfiable);}
  addi[1].has_imm=false;fails(select(addi,3,riscv.rules,"value"),Error::Code::Unsatisfiable);
  // Full nested addressing remains a single instruction; the width-specific
  // instruction identity and memory contract survive the covering.
  constant.imm=4;
  std::vector<Node> load={input(1),constant,operation(3,"SEM_addr_2",{1,2},""),operation(4,"TAG_symbol_target",{},""),operation(5,"ISA_lw",{4,3})};
  load[4].access=schedrow::MemoryAccess{};load[4].access->read=true;load[4].access->size=4;load[4].side_effect=true;
  auto memory=take(emit_scheduler(load,riscv.rules,take(select(load,5,riscv.rules,"value"))));
  CHECK(memory.instructions.size()==1&&memory.instructions[0].opcode=="lw"&&memory.instructions[0].access->size==4&&memory.instructions[0].uses==std::vector<uint32_t>{1});
  CHECK(memory.instructions[0].immediates==std::vector<std::pair<uint32_t,int64_t>>({{2,4}})&&!memory.instructions[0].speculative);
  std::vector<Node> branch={input(1),input(2),operation(3,"SEM_eq_2",{1,2},""),constant,operation(4,"SEM_branch_1",{2},""),operation(5,"ISA_beq",{3,4},"")};
  branch[3].id=6;branch[4].children={6};branch.back().produces_value=false;branch.back().side_effect=true;branch.back().terminator=true;
  for(auto offset:{int64_t(-4096),int64_t(4094)}){branch[3].imm=offset;take(select(branch,5,riscv.rules,"stmt"));}
  for(auto offset:{int64_t(-4098),int64_t(4096),int64_t(3)}){branch[3].imm=offset;fails(select(branch,5,riscv.rules,"stmt"),Error::Code::Unsatisfiable);}
  branch[3].imm=4;auto control=take(emit_scheduler(branch,riscv.rules,take(select(branch,5,riscv.rules,"stmt"))));CHECK(control.instructions[0].terminator&&control.instructions[0].defs.empty());
  auto wasm=take(load_rules_file((root/"limeburg/specs/wasm.lburg").string()));
  // Stack semantics remain opaque to generic arithmetic matching.
  std::vector<Node> stack={operation(1,"TAG_string_add",{},""),operation(2,"ISA_i32_5fadd",{1},"")};stack[1].side_effect=true;stack[1].produces_value=false;
  auto stack_result=take(emit_scheduler(stack,wasm.rules,take(select(stack,2,wasm.rules,"stmt"))));CHECK(stack_result.instructions[0].opcode=="i32_add"&&stack_result.instructions[0].barrier);
  auto architecture=fixture();auto generated=take(from_infobank(architecture));CHECK(generated.coverage[0].mode=="inventory");
  CHECK(generated.document.rules.rules[0].cost==7&&generated.document.rules.rules[0].pattern->children[1].immediate==std::pair<int64_t,int64_t>{-8,7});
  auto missing=architecture;missing.encodings["E"].erase("signed");auto skipped=take(from_infobank(missing));CHECK(skipped.coverage[0].reason.find("signedness")!=std::string::npos&&!skipped.coverage[0].rule);
  auto missing_effect=architecture;section(missing_effect,"vmm").erase("may_trap");CHECK(take(from_infobank(missing_effect)).coverage[0].reason.find("missing VMM effect")!=std::string::npos);
  auto unsigned_immediate=architecture;unsigned_immediate.encodings["E"]["signed"]=metacode::Value(int64_t(0));CHECK(take(from_infobank(unsigned_immediate)).document.rules.rules[0].pattern->children[1].immediate==std::pair<int64_t,int64_t>{0,15});
  auto multiple=architecture;section(multiple,"dataflow")["defs"]=metacode::Value(metacode::Value::Array{metacode::Value(std::string("rd")),metacode::Value(std::string("rs"))});CHECK(take(from_infobank(multiple)).coverage[0].reason.find("multiple results")!=std::string::npos);
  auto malformed=architecture;malformed.operations[0].semantics="(set rd (add rs imm)";fails(from_infobank(malformed),Error::Code::Parse);
  auto deep=architecture;deep.operations[0].semantics=std::string(129,'(')+std::string(129,')');fails(from_infobank(deep),Error::Code::ResourceLimit);
  auto mismatch=architecture;section(mismatch,"dataflow")["uses"]=metacode::Value(metacode::Value::Array{metacode::Value(std::string("rs"))});CHECK(take(from_infobank(mismatch)).coverage[0].reason.find("absent from dataflow")!=std::string::npos);
  auto invalid_cost=architecture;section(invalid_cost,"instruction_selection")["cost"]=metacode::Value(int64_t(-1));fails(from_infobank(invalid_cost),Error::Code::InvalidArgument);
  auto predicate=architecture;section(predicate,"instruction_selection")["where"]=metacode::Value(metacode::operand_constraints_metadata(std::vector<metacode::OperandConstraint>{{metacode::OperandPredicate::MultipleOf,"imm",{},int64_t(2)}}));
  auto constrained=take(from_infobank(predicate));CHECK(constrained.document.rules.rules[0].constraints.size()==1);
  std::vector<Node> values={input(1,"G"),constant,operation(3,"ISA_plus",{1,2},"G")};values[1].imm=2;CHECK(take(select(values,3,constrained.document.rules,"value")).cost==7);
  values[1].imm=3;fails(select(values,3,constrained.document.rules,"value"),Error::Code::Unsatisfiable);
  auto gap=architecture;gap.encodings["E"].erase("imm");gap.encodings["E"]["imm[3:2]"]=metacode::Value(std::string("8:2"));CHECK(!take(from_infobank(gap)).coverage[0].rule);
  auto explicit_tree=architecture;section(explicit_tree,"instruction_selection")["selection_tree"]=metacode::Value(std::string("add(?rs register_class G, const():i32[-2..3] binding imm):i32 register_class G"));
  auto explicit_spec=take(from_infobank(explicit_tree));CHECK(explicit_spec.coverage[0].mode=="explicit"&&explicit_spec.document.rules.rules[0].op=="add");
  CHECK(take(print_infobank_spec(explicit_spec)).find("fixture.isa:")!=std::string::npos);
  auto malformed_spec=explicit_spec;malformed_spec.machine.instructions.at("plus")["tooling"]=metacode::Value(int64_t(1));fails(print_infobank_spec(malformed_spec),Error::Code::InvalidArgument);
  auto virtual_isa=take(metacode::parse_isa("arch virtual {} profile { register_classes=[\"G\"]; } op nop {tooling={instruction_selection={selection_tree=\"nop()\";};};}","virtual.isa"));
  auto normalized=take(unisel::from_metacode(virtual_isa));CHECK(normalized.register_classes.contains("G")&&normalized.register_classes.at("G").empty());
  CHECK(take(unisel::load_umd(unisel::print_umd(normalized))).machine.register_classes.contains("G"));
  auto bad_classes=virtual_isa;std::get<metacode::Value::Object>(bad_classes.fields.at("profile").data)["register_classes"]=metacode::Value(metacode::Value::Array{metacode::Value(std::string("G")),metacode::Value(std::string("G"))});fails(unisel::from_metacode(bad_classes),Error::Code::InvalidArgument);
});}
