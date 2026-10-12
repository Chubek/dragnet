#include "test.hpp"
#include "tunah.hpp"
#include <array>
#include <bit>
#include <random>

using namespace limestone;
using namespace limestone::tunah;
namespace {
uint64_t mask(unsigned width) { return width==64?~uint64_t{0}:(uint64_t{1}<<width)-1; }
int64_t signed_value(uint64_t value, unsigned width) {
  value&=mask(width);
  if(width==64) return std::bit_cast<int64_t>(value);
  return value&(uint64_t{1}<<(width-1))?static_cast<int64_t>(value)-static_cast<int64_t>(uint64_t{1}<<width):static_cast<int64_t>(value);
}
using Values=std::map<std::string,uint64_t>;

// Independent reference semantics for the wrapping integer vocabulary and
// explicit-width conversions. Values are bits, avoiding signed C++ overflow.
uint64_t evaluate(const Term& term, const Values& values, unsigned width) {
  if(term.constant) return static_cast<uint64_t>(*term.constant)&mask(width);
  if(term.arguments.empty()) return values.at(term.op)&mask(width);
  const auto& op=term.op;
  auto child=[&](size_t i) { return evaluate(term.arguments.at(i),values,width); };
  static const std::map<std::string,std::pair<unsigned,unsigned>> conversions{
    {"sext8to32",{8,32}},{"zext8to32",{8,32}},{"sext8to64",{8,64}},{"zext8to64",{8,64}},
    {"sext32to64",{32,64}},{"zext32to64",{32,64}},{"trunc32to8",{32,8}},{"trunc64to32",{64,32}}
  };
  if(auto conversion=conversions.find(op);conversion!=conversions.end()) {
    auto [source,destination]=conversion->second;
    if(destination!=width) throw std::runtime_error("ill-typed conversion "+op+" in "+std::to_string(width)+"-bit context");
    auto value=evaluate(term.arguments.at(0),values,source);
    return (op.starts_with("sext")?static_cast<uint64_t>(signed_value(value,source)):value)&mask(width);
  }
  uint64_t result=0;
  if(op=="iadd") result=child(0)+child(1);
  else if(op=="isub") result=child(0)-child(1);
  else if(op=="imul") result=child(0)*child(1);
  else if(op=="iand") result=child(0)&child(1);
  else if(op=="ior") result=child(0)|child(1);
  else if(op=="ixor") result=child(0)^child(1);
  else if(op=="ineg") result=uint64_t{0}-child(0);
  else if(op=="inot") result=~child(0);
  else if(op=="ishl") result=child(0)<<(child(1)%width);
  else if(op=="ishr") result=child(0)>>(child(1)%width);
  else if(op=="isar") {
    auto value=child(0),count=child(1)%width;
    result=value>>count;
    if(count&&(value&(uint64_t{1}<<(width-1)))) result|=~uint64_t{0}<<(width-count);
  } else if(op=="udiv"||op=="urem") {
    auto a=child(0),b=child(1);CHECK(b!=0);result=op=="udiv"?a/b:a%b;
  } else if(op=="sdiv"||op=="srem") {
    auto a=signed_value(child(0),width),b=signed_value(child(1),width);CHECK(b!=0);
    if(a==std::numeric_limits<int64_t>::min()&&b==-1) result=op=="sdiv"?static_cast<uint64_t>(a):0;
    else result=static_cast<uint64_t>(op=="sdiv"?a/b:a%b);
  } else if(op=="copy") result=child(0);
  else if(op=="ite") result=child(0)?child(1):child(2);
  else if(op=="sel") result=child(2)?child(0):child(1);
  else if(op=="wide-add") result=static_cast<uint64_t>(signed_value(child(0),32)+signed_value(child(1),32));
  else throw std::runtime_error("unmodeled reference operator: "+op);
  return result&mask(width);
}
void variables(const Term& term, Values& values) {
  if(!term.op.empty()&&term.op.front()=='?') values.emplace(term.op,0);
  for(const auto& child:term.arguments) variables(child,values);
}
Term instantiate(Term term, const Values& values) {
  if(!term.op.empty()&&term.op.front()=='?') {
    term.constant=std::bit_cast<int64_t>(values.at(term.op));term.op.clear();
  }
  for(auto& child:term.arguments) child=instantiate(std::move(child),values);
  return term;
}
bool fits32(std::string_view operation, std::span<const Term> arguments) {
  for(const auto& argument:arguments) if(!argument.constant) return false;
  int64_t a=signed_value(static_cast<uint64_t>(*arguments[0].constant),32),result=0;
  if(operation=="neg") result=-a;
  else {
    int64_t b=signed_value(static_cast<uint64_t>(*arguments[1].constant),32);
    if(operation=="add") result=a+b;
    else if(operation=="sub") result=a-b;
    else if(operation=="mul") result=a*b;
    else throw std::runtime_error("unknown range predicate");
  }
  return result>=std::numeric_limits<int32_t>::min()&&result<=std::numeric_limits<int32_t>::max();
}
void analyses(Session& session) {
  for(auto operation:{"add","sub","mul","neg"}) {
    take(session.define_predicate(std::string("signed-")+operation+"-fits32",std::string_view(operation)=="neg"?1:2,
      [operation](std::span<const Term> arguments) { return Result<bool>::ok(fits32(operation,arguments)); }));
  }
  for(auto operation:{"add","sub","mul"}) {
    // This scalar test host has no vector-lane analysis. It never proves a
    // vector precondition; registering the adapter still validates its arity.
    take(session.define_predicate(std::string("vector-signed-")+operation+"-fits",2,
      [](auto) { return Result<bool>::ok(false); }));
  }
}
unsigned result_width(const RewriteRule& rule) {
  if(rule.lhs.op=="trunc32to8") return 8;
  if(rule.lhs.op=="trunc64to32") return 32;
  if(rule.lhs.op=="sext32to64"||rule.lhs.op=="zext32to64") return 64;
  return 32;
}
void arithmetic_soundness(const Session& session, std::mt19937_64& random) {
  for(const auto& rule:session.rules()) {
    for(unsigned width:{8,16,32,64}) {
      Values values;variables(rule.lhs,values);
      std::array<uint64_t,8> boundaries{0,1,2,mask(width),mask(width)-1,mask(width)>>1,uint64_t{1}<<(width-1),255};
      for(size_t trial=0;trial<256;++trial) {
        size_t index=0;
        for(auto& [name,value]:values) { value=(trial<boundaries.size()?boundaries[(trial+index++)%boundaries.size()]:random())&mask(width); }
        if(evaluate(rule.lhs,values,width)!=evaluate(rule.rhs,values,width)) throw std::runtime_error("unsound arithmetic rule: "+rule.name+" at width "+std::to_string(width));
      }
      // Exercise the real e-graph and extraction for every pure rule, checking
      // its output's denotation rather than asserting a particular spelling.
      Session isolated;take(isolated.load_rules_file(std::filesystem::path(rule.location.file).parent_path()/"vocabulary.tuner"));isolated.add_rule(rule);
      auto input=instantiate(rule.lhs,values);
      auto output=take(isolated.saturate(input));
      CHECK(evaluate(input,{},width)==evaluate(output.term,{},width));
      CHECK(output.saturated&&!output.limit_reached);
    }
  }
}
struct Memory {
  std::map<uint64_t,uint8_t> bytes;
  bool operator==(const Memory&) const = default;
};
uint64_t memory_evaluate(const Term& term, const Values& values, Memory& memory) {
  if(term.op=="seq") { memory_evaluate(term.arguments[0],values,memory);return memory_evaluate(term.arguments[1],values,memory); }
  if(term.op.starts_with("store")||term.op.starts_with("load")) {
    auto address=evaluate(term.arguments[0],values,64);
    auto width=std::stoul(term.op.substr(term.op.starts_with("store")?5:4));
    if(term.op.starts_with("store")) {
      auto value=evaluate(term.arguments[1],values,64);
      for(size_t i=0;i<width/8;++i) memory.bytes[address+i]=static_cast<uint8_t>(value>>(8*i));
      return 0;
    }
    uint64_t result=0;
    for(size_t i=0;i<width/8;++i) result|=static_cast<uint64_t>(memory.bytes[address+i])<<(8*i);
    return result;
  }
  return evaluate(term,values,64);
}
}

int main(int argc,char** argv) { return test_main([&] {
  CHECK(argc==2);std::filesystem::path directory=argv[1];
  Session corpus;analyses(corpus);CHECK(take(corpus.load_rules_file(directory/"vocabulary.tuner"))==0);
  std::vector<std::filesystem::path> files;
  for(const auto& entry:std::filesystem::directory_iterator(directory)) if(entry.path().extension()==".tuner"&&entry.path().filename()!="vocabulary.tuner") files.push_back(entry.path());
  std::sort(files.begin(),files.end());CHECK(files.size()==13);
  size_t count=0;
  for(const auto& file:files) { auto loaded=take(corpus.load_rules_file(file));CHECK(loaded>0);count+=static_cast<size_t>(loaded); }
  CHECK(corpus.rules().size()==count);
  for(const auto& rule:corpus.rules()) CHECK(rule.location.line>0&&rule.location.column==1&&rule.location.file.ends_with(".tuner")&&!rule.location.expanded);
  std::mt19937_64 random(0x4c696d6573746f6eULL);
  Session pure;take(pure.load_rules_file(directory/"vocabulary.tuner"));
  for(auto file:{"alg-sim.tuner","const-folding.tuner","codesize-reduce.tuner","strength-reduct.tuner","uarch-opt.tuner"}) take(pure.load_rules_file(directory/file));
  arithmetic_soundness(pure,random);

  Session climbing;analyses(climbing);take(climbing.load_rules_file(directory/"vocabulary.tuner"));take(climbing.load_rules_file(directory/"instr-climbing.tuner"));
  for(const auto& rule:climbing.rules()) {
    Values values;variables(rule.lhs,values);
    for(size_t trial=0;trial<256;++trial) {
      for(auto& [name,value]:values) value=random();
      bool allowed=true;
      for(const auto& condition:rule.conditions) {
        std::vector<Term> arguments;for(const auto& argument:condition.arguments) arguments.push_back(instantiate(argument,values));
        auto operation=condition.predicate.substr(7,condition.predicate.size()-14);
        allowed&=fits32(operation,arguments);
      }
      if(allowed) CHECK(evaluate(rule.lhs,values,result_width(rule))==evaluate(rule.rhs,values,result_width(rule)));
    }
  }
  CHECK(take(climbing.saturate("(trunc64to32 (zext32to64 x))")).expression=="x");
  CHECK(take(climbing.saturate("(sext32to64 (sext8to32 x))")).expression=="(sext8to64 x)");
  // Force the wider form to pay only after a verified distribution exposes
  // a fused operation. Overflowing narrow arithmetic must retain its shape.
  take(climbing.load_rules("(operator wide-add 2) (rule fuse-wide (iadd (sext32to64 ?x) (sext32to64 ?y)) (wide-add ?x ?y))"));
  CostModel wide;wide.operators={{"sext32to64",10},{"iadd",10},{"wide-add",1}};
  CHECK(take(climbing.saturate("(sext32to64 (iadd 1 2))",{},wide)).expression=="(wide-add 1 2)");
  auto overflow=take(climbing.saturate("(sext32to64 (iadd 2147483647 1))",{},wide));
  CHECK(overflow.expression=="(sext32to64 (iadd 2147483647 1))"&&overflow.rewrites==0);
  CHECK(evaluate(overflow.term,{},64)==0xffffffff80000000ULL);
  for(auto expression:{"(sext32to64 (isub -2147483648 1))","(sext32to64 (imul 1073741824 2))","(sext32to64 (ineg -2147483648))"}) {
    auto unchanged=take(climbing.saturate(expression));CHECK(unchanged.expression==expression&&unchanged.rewrites==0);
  }
  Session unavailable;take(unavailable.load_rules_file(directory/"vocabulary.tuner"));
  fails(unavailable.load_rules_file(directory/"instr-climbing.tuner"),Error::Code::Unsupported);CHECK(unavailable.rules().empty());
  fails(unavailable.load_rules_file(directory/"autovect.tuner"),Error::Code::Unsupported);CHECK(unavailable.rules().empty());

  Session windows;take(windows.load_rules_file(directory/"vocabulary.tuner"));take(windows.load_rules_file(directory/"peephole-opt.tuner"));
  CostModel memory_costs;memory_costs.operators={{"load8",20},{"load32",20},{"load64",20}};
  for(auto width:{8,32,64}) {
    for(auto value:{uint64_t{0},uint64_t{255},uint64_t{256},uint64_t{0x100000001},~uint64_t{0}}) {
      auto expression="(seq (store"+std::to_string(width)+" p v) (load"+std::to_string(width)+" p))";
      auto input=take(parse_term(expression));auto output=take(windows.saturate(input,{},memory_costs));
      Values values{{"p",32},{"v",value}};Memory before{{{0,17},{64,23}}},after=before;
      auto expected=memory_evaluate(input,values,before),actual=memory_evaluate(output.term,values,after);
      CHECK(actual==expected&&before==after&&actual==(value&mask(width)));
      CHECK(output.expression.find("load")==std::string::npos&&output.expression.find("store")!=std::string::npos);
    }
  }

  Session vectors;analyses(vectors);take(vectors.load_rules_file(directory/"vocabulary.tuner"));take(vectors.load_rules_file(directory/"autovect.tuner"));
  CHECK(take(vectors.saturate("(av-broadcast (iadd x 0))")).expression=="(av-broadcast x)");
  CHECK(take(vectors.saturate("(av-broadcast x)")).expression=="(av-broadcast x)");
  CHECK(take(vectors.saturate("(av-sext (av-add a b))")).expression=="(av-sext (av-add a b))");
  // The reduction of a vector sum is a scalar sum, including wrap at 8 bits.
  CostModel reduction_costs;reduction_costs.operators={{"av-add",20},{"iadd",1},{"av-reduce-add",1}};
  auto reduction=take(vectors.saturate("(av-reduce-add (av-add a b))",{},reduction_costs));
  CHECK(reduction.expression=="(iadd (av-reduce-add a) (av-reduce-add b))");
  for(size_t trial=0;trial<256;++trial) {
    uint64_t combined=0,a=0,b=0;
    for(int lane=0;lane<4;++lane) { auto x=random()&255,y=random()&255;combined=(combined+((x+y)&255))&255;a=(a+x)&255;b=(b+y)&255; }
    CHECK(combined==((a+b)&255));
  }
  std::cout<<"validated "<<count<<" rules in "<<files.size()<<" tuner files; checked "<<pure.rules().size()<<" wrapping-integer rules at 8/16/32/64 bits\n";
}); }
