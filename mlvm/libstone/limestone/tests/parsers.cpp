#include "test.hpp"
#include "parsers/bin2bin_ast.hpp"
#include "parsers/isa_ast.hpp"
#include "parsers/limeburg_ast.hpp"
#include "parsers/limestone_ast.hpp"
#include "parsers/machine-ir_ast.hpp"
#include "parsers/regtl_ast.hpp"
#include "parsers/schedrow_ast.hpp"
#include "parsers/traceml_ast.hpp"
#include "parsers/tuner_ast.hpp"
#include "parsers/unisel_ast.hpp"
#include "limestone/limestone.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace syntax=limestone::syntax;
std::string read(const std::filesystem::path& path) {
  std::ifstream input(path);CHECK(input.good());std::ostringstream buffer;buffer<<input.rdbuf();return buffer.str();
}
template<class Alternative,class Sum> Alternative& alternative(Sum& sum) {
  auto* pointer=std::get_if<std::unique_ptr<Alternative>>(&sum.value);CHECK(pointer&&*pointer);return **pointer;
}
// All generated walkers expose the same enter/leave interface, with language-local Node types.
#define CHECK_SPANS(Language, Doc, Text, File) do { \
  struct Walk final : syntax::Language::ConstRecursiveVisitor { \
    std::string_view input, filename; size_t count=0; \
    bool enter(const syntax::Language::Node& node) override { \
      const auto& s=node.source; CHECK(s.file==filename); \
      CHECK(s.begin.offset<=s.end.offset&&s.end.offset<=input.size()); \
      auto position=[&](size_t offset) { syntax::SourcePosition p; \
        for(size_t i=0;i<offset;++i) { if(input[i]=='\n') { ++p.line;p.column=1; } else ++p.column; } \
        p.offset=offset;return p; }; \
      CHECK(s.begin==position(s.begin.offset)&&s.end==position(s.end.offset));++count;return true; \
    } \
  }; \
  Walk walk;walk.input=Text;walk.filename=File;std::as_const(*Doc).accept(walk);CHECK(walk.count>3); \
} while(false)

template<class Parse> void invalid(Parse parse,std::initializer_list<std::string_view> texts) {
  for(auto text:texts) { auto result=parse(text,"bad.input",syntax::ParseLimits{});fails(result,limestone::Error::Code::Parse);CHECK(result.error().message.starts_with("bad.input:")); }
}

int main(int argc,char** argv) {return test_main([&]{
  CHECK(argc==2);const auto root=std::filesystem::path(argv[1]);const auto examples=root/"parsers/examples";
  auto isa_text=read(examples/"isa.isa");auto isa=take(syntax::isa::parse(isa_text,"isa.isa"));
  CHECK(isa->declarations.size()==9);
  auto& arch=alternative<syntax::isa::Arch>(*isa->declarations[0]);CHECK(arch.name->value=="toy");
  CHECK(arch.fields->entries.size()==3);
  auto& regclass=alternative<syntax::isa::Regclass>(*isa->declarations[5]);
  CHECK(regclass.registers->entries[1]->width->value=="64"&&regclass.registers->entries[1]->number->value=="1");
  auto& op=alternative<syntax::isa::Op>(*isa->declarations.back());CHECK(op.fields->entries[3]->value_data==nullptr);
  CHECK_SPANS(isa,isa,isa_text,"isa.isa");

  auto trace_text=read(examples/"traceml.trace");auto trace=take(syntax::traceml::parse(trace_text,"trace"));
  CHECK(trace->forms.size()==2);CHECK_SPANS(traceml,trace,trace_text,"trace");
  struct Rename : syntax::traceml::RecursiveVisitor {
    void visit(syntax::traceml::Symbol& symbol) override { if(symbol.value=="twice")symbol.value="double"; }
  } rename;
  trace->accept(rename);
  struct Count : syntax::traceml::ConstRecursiveVisitor {
    size_t renamed=0;
    void visit(const syntax::traceml::Symbol& symbol) override { if(symbol.value=="double")++renamed; }
  } count;
  std::as_const(*trace).accept(count);CHECK(count.renamed==2);
  struct Prune : syntax::traceml::ConstRecursiveVisitor {
    size_t entered=0,left=0;
    bool enter(const syntax::traceml::Node& node) override { ++entered;return node.kind()!=syntax::traceml::Kind::List; }
    void leave(const syntax::traceml::Node&) override { ++left; }
  } prune;
  std::as_const(*trace).accept(prune);CHECK(prune.entered==5&&prune.left==3);

  auto tuner_text=read(examples/"tuner.tuner");auto tuner=take(syntax::tuner::parse(tuner_text,"tuner"));
  CHECK(tuner->declarations.size()==4);
  auto& rule=alternative<syntax::tuner::Rule>(*tuner->declarations[3]);
  CHECK(rule.name->value=="guarded"&&rule.clauses.size()==2);
  CHECK(alternative<syntax::tuner::Cost>(*rule.clauses[1]).delta->value=="-1");
  CHECK_SPANS(tuner,tuner,tuner_text,"tuner");

  auto burg_text=read(examples/"limeburg.limeburg");auto burg=take(syntax::limeburg::parse(burg_text,"burg"));
  CHECK(burg->declarations.size()==3);
  auto& tree=alternative<syntax::limeburg::Tree>(*burg->declarations[2]);
  CHECK(tree.nodes.size()==3&&tree.nodes[2]->operands.size()==2);
  CHECK(tree.nodes[2]->result->value=="%3"&&tree.nodes[2]->operands[0]->value=="%1");
  auto& burg_rules=alternative<syntax::limeburg::RuleSet>(*burg->declarations[1]);
  auto& leaf_rule=alternative<syntax::limeburg::Rule>(*burg_rules.declarations[4]);
  CHECK(leaf_rule.pattern->arguments&&leaf_rule.pattern->arguments->children.empty());
  auto& add_rule=alternative<syntax::limeburg::Rule>(*burg_rules.declarations[5]);
  CHECK(add_rule.pattern->arguments->children.size()==2&&!add_rule.pattern->arguments->children[0]->arguments);
  CHECK_SPANS(limeburg,burg,burg_text,"burg");

  auto unisel_text=read(examples/"unisel.umd");auto unisel=take(syntax::unisel::parse(unisel_text,"umd"));
  CHECK(unisel->declarations.size()==3);
  auto& machine=alternative<syntax::unisel::Machine>(*unisel->declarations[1]);
  CHECK(machine.items.size()==7);
  CHECK(alternative<syntax::unisel::RegisterClass>(*machine.items[0]).members[0]->value=="$r0");
  CHECK_SPANS(unisel,unisel,unisel_text,"umd");

  auto schedule_text=read(examples/"schedrow.schedrow");auto schedule=take(syntax::schedrow::parse(schedule_text,"schedule"));
  CHECK(schedule->declarations.size()==5);
  auto& region=alternative<syntax::schedrow::Region>(*schedule->declarations[1]);
  auto& dependency=alternative<syntax::schedrow::Dependency>(*region.items[3]);
  CHECK(dependency.attributes.size()==6);
  CHECK(alternative<syntax::schedrow::Integer>(*dependency.attributes[4]->value_data).value=="1");
  CHECK(alternative<syntax::schedrow::Boolean>(*dependency.attributes[5]->value_data).value=="false");
  CHECK_SPANS(schedrow,schedule,schedule_text,"schedule");

  auto regtl_text=read(examples/"regtl.regtl");auto registers=take(syntax::regtl::parse(regtl_text,"regtl"));
  CHECK(registers->units.size()==1);
  auto& live=alternative<syntax::regtl::LiveRange>(*registers->units[0]->declarations[2]);
  CHECK(live.begin->value=="0"&&live.end->value=="4"&&live.constraints.size()==5);
  CHECK_SPANS(regtl,registers,regtl_text,"regtl");

  auto binary_text=read(examples/"bin2bin.bin2bin");auto binary=take(syntax::bin2bin::parse(binary_text,"binary"));
  CHECK(binary->declarations.size()==4);
  auto& translation=alternative<syntax::bin2bin::Translation>(*binary->declarations[2]);
  CHECK(translation.items.size()==4);
  CHECK(alternative<syntax::bin2bin::Rule>(*translation.items[2]).options.size()==2);
  CHECK_SPANS(bin2bin,binary,binary_text,"binary");

  auto limestone_text=read(examples/"limestone.limestone");auto core=take(syntax::limestone::parse(limestone_text,"core"));
  CHECK(core->declarations.size()==4);CHECK_SPANS(limestone,core,limestone_text,"core");
  auto mir_text=read(examples/"machine-ir.mir");auto mir=take(syntax::machine_ir::parse(mir_text,"mir"));
  CHECK(mir->declarations.size()==3);CHECK_SPANS(machine_ir,mir,mir_text,"mir");
  // The real pipeline's emitted machine-facing text is accepted by both module grammars.
  auto module=take(limestone::run_pipeline("(add 1 2)"));
  CHECK(take(syntax::machine_ir::parse(module.machine_ir))->declarations.size()==1);
  CHECK(take(syntax::limestone::parse(module.machine_ir))->declarations.size()==1);
  // EOF comments, comment delimiters inside strings, and block comments are lexical boundaries.
  CHECK(take(syntax::isa::parse("arch a {} # eof"))->declarations.size()==1);
  CHECK(take(syntax::traceml::parse("(-1 2) ; eof"))->forms.size()==1);
  CHECK(take(syntax::tuner::parse("(operator x 0) ; eof"))->declarations.size()==1);
  CHECK(take(syntax::limeburg::parse("/* block */ nonterminal reg; // eof"))->declarations.size()==1);
  CHECK(take(syntax::unisel::parse("// line\nmachine m {} // eof"))->declarations.size()==1);
  CHECK(take(syntax::schedrow::parse("/* block */ region %r {} // eof"))->declarations.size()==1);
  CHECK(take(syntax::regtl::parse("regtl r {} // eof"))->units.size()==1);
  CHECK(take(syntax::bin2bin::parse("architecture a {} # eof"))->declarations.size()==1);
  CHECK(take(syntax::limestone::parse("module m {} ; eof"))->declarations.size()==1);
  CHECK(take(syntax::machine_ir::parse("/* block */ module m {} // eof"))->declarations.size()==1);
  auto string_doc=take(syntax::limeburg::parse("include \"http://x/*y*/\";"));
  CHECK(alternative<syntax::limeburg::Include>(*string_doc->declarations[0]).path->value=="\"http://x/*y*/\"");

  invalid(syntax::isa::parse,{"arch a { x = [1,; }","regclass G { r(64)=, }","arch a { x = \"unterminated; }"});
  invalid(syntax::traceml::parse,{"()","(lambda x","(a))","\"text\""});
  invalid(syntax::tuner::parse,{"(rule missing (iadd ?x 0))","(operator add -1)","(rule r ?x ?x :unknown 1)","(rule r (?x 1) 1)"});
  invalid(syntax::limeburg::parse,{"rule reg: ADD(reg,) -> ADD cost 1;","tree t { node %1 = CONST(); }","rule reg: X -> X cost;","nonterminal reg; /* unterminated"});
  invalid(syntax::unisel::parse,{"program p { node %1 = add(%2,); }","machine m { pattern p: add(?x) -> ; }","program p { output; }"});
  invalid(syntax::schedrow::parse,{"region %r { instruction %i { def } }","machine_model m { issue { width = ; } }","region %r { dependency { producer = [1,]; } }"});
  invalid(syntax::regtl::parse,{"regtl p { live %v:G [1,] {} }","regtl p { move %v <- ; }","regtl p { regclass G = [$r,]; }"});
  invalid(syntax::bin2bin::parse,{"translate a -> { }","unit u { instruction 0: NOP [0,]; }","architecture a { opcode 0 NOP status guessed; }"});
  invalid(syntax::limestone::parse,{"module m { function f { block b { %1 = } } }","pipeline p { stage guessed }","module m {"});
  invalid(syntax::machine_ir::parse,{"module m { function f { register %r:G width -1 } }","module m { function f { block b { %1 = load [] } } }","module m { function f { block b { %1 = op # } } }"});
  // Recoverable errors never poison subsequent parses, and AST strings outlive input buffers.
  auto owned=take(syntax::traceml::parse(std::string("(a 7)"),std::string("owned.trace")));
  auto& list=alternative<syntax::traceml::List>(*owned->forms[0]);CHECK(list.elements.size()==2);
  CHECK(alternative<syntax::traceml::Symbol>(*list.elements[0]).value=="a");
  CHECK(alternative<syntax::traceml::Integer>(*list.elements[1]).source.file=="owned.trace");
  auto bad=syntax::traceml::parse("\n\n(a","location.trace");CHECK(!bad);
  CHECK(bad.error().message.starts_with("location.trace:3:"));
  fails(syntax::traceml::parse(std::string_view("a\0b",3)),limestone::Error::Code::Parse);
  syntax::ParseLimits limits;limits.bytes=2;
  fails(syntax::traceml::parse("(x)","limits",limits),limestone::Error::Code::ResourceLimit);
  limits={};limits.nodes=1;
  fails(syntax::traceml::parse("(x)","limits",limits),limestone::Error::Code::ResourceLimit);
  limits={};limits.depth=3;
  fails(syntax::traceml::parse("(x)","limits",limits),limestone::Error::Code::ResourceLimit);
  limits={};limits.depth=0;
  fails(syntax::traceml::parse("(x)","limits",limits),limestone::Error::Code::InvalidArgument);

  size_t isas=0,rules=0;
  for(const auto& entry:std::filesystem::directory_iterator(root/"metacode/infobank/isa")) {
    if(entry.path().extension()!=".isa"||entry.path().filename().string().front()=='.')continue;
    auto text=read(entry.path());auto parsed=syntax::isa::parse(text,entry.path().string());
    if(entry.path().filename()=="dsl.lua.isa") {
      // This legacy marker is syntactically valid; Metacode separately requires an architecture.
      auto document=take(std::move(parsed));CHECK(document->declarations.size()==1);
      CHECK(std::holds_alternative<std::unique_ptr<syntax::isa::Legacy>>(document->declarations[0]->value));continue;
    }
    auto document=take(std::move(parsed));CHECK(!document->declarations.empty());++isas;
  }
  for(const auto& entry:std::filesystem::directory_iterator(root/"tunah/tuners/instr-level-tune")) {
    if(entry.path().extension()!=".tuner")continue;
    auto document=take(syntax::tuner::parse(read(entry.path()),entry.path().string()));
    for(const auto& declaration:document->declarations)
      if(std::holds_alternative<std::unique_ptr<syntax::tuner::Rule>>(declaration->value))++rules;
  }
  CHECK(isas==23&&rules==239);
});}
