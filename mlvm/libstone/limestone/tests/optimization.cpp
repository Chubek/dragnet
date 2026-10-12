#include "test.hpp"
#include "tunah.hpp"

int main(){return test_main([]{
  using namespace limestone;using namespace tunah;
  Session session;session.define_operator("add",2);
  CHECK(take(session.load_rules("(rule zero (add ?x 0) ?x)"))==1);
  auto optimized=take(session.saturate("(add (add x 0) 0)"));
  CHECK(optimized.expression=="x"&&optimized.cost==1&&optimized.saturated&&optimized.rewrites>=2&&!optimized.trace.empty());
  CHECK(optimized.derivation_nodes.size()==optimized.nodes&&!optimized.derivation_steps.empty());
  CHECK(std::any_of(optimized.derivation_steps.begin(),optimized.derivation_steps.end(),[](auto& step){return step.rule=="zero"&&!step.bindings.empty();}));
  CHECK(print_derivation(take(session.saturate("(add (add x 0) 0)")))==print_derivation(optimized));
  auto located=take(session.saturate(take(parse_term("(add\n (add x 0)\n 0)","origins.term"))));
  auto literal=std::find_if(located.derivation_nodes.begin(),located.derivation_nodes.end(),[](auto& node){return node.constant==0;});CHECK(literal!=located.derivation_nodes.end()&&literal->location.line==2&&literal->origins.size()==2&&literal->origins.back().line==3);
  Session congruence;take(congruence.load_rules("(operator add 2) (operator wrap 1) (operator pair 2) (rule zero (add ?x 0) ?x)"));
  auto witnesses=take(congruence.saturate("(pair (wrap (add x 0)) (wrap x))"));
  CHECK(std::any_of(witnesses.derivation_steps.begin(),witnesses.derivation_steps.end(),[](auto& step){return step.rule.empty()&&step.lhs_node&&step.rhs_node;}));
  bool saw_partial=false;
  for(size_t stop=1;stop<180;++stop) {
    size_t checks=0;Limits limit;limit.cancelled=[&]{return ++checks==stop;};auto result=take(session.saturate("(add (add (add x 0) 0) 0)",limit));
    auto committed=std::count_if(result.derivation_steps.begin(),result.derivation_steps.end(),[](auto& step){return !step.rule.empty();});CHECK(result.rewrites==size_t(committed));
    if(result.graph_discarded){CHECK(result.limit_reached&&!result.saturated&&result.expression=="(add (add (add x 0) 0) 0)"&&checks==stop);saw_partial|=result.rewrites>0;}
  }
  CHECK(saw_partial);
  for(int i=0;i<5;++i)CHECK(take(session.saturate("(add (add x 0) 0)")).expression==optimized.expression);
  CHECK(take(session.saturate("(add 2 3)")).expression=="(add 2 3)");
  auto before=session.rules().size();fails(session.load_rules("(rule unbound (add ?x 0) ?y)"),Error::Code::InvalidArgument);CHECK(session.rules().size()==before);
  fails(session.load_rules("(rule zero (add ?x 0) ?x)"),Error::Code::Conflict);
  fails(session.load_rules("(rule bad (unknown ?x) ?x)"),Error::Code::InvalidArgument);
  fails(session.saturate("(add x)"),Error::Code::InvalidArgument);
  fails(session.saturate("(add x 0"),Error::Code::Parse);
  fails(session.saturate("x y"),Error::Code::Parse);
  fails(session.saturate("?x"),Error::Code::InvalidArgument);
  Limits none;none.iterations=0;auto unchanged=take(session.saturate("(add x 0)",none));CHECK(unchanged.expression=="(add x 0)"&&unchanged.limit_reached&&!unchanged.saturated);
  Limits cancelled;cancelled.cancelled=[] {return true;};auto stopped=take(session.saturate("(add x 0)",cancelled));CHECK(stopped.expression=="(add x 0)"&&stopped.limit_reached);
  Limits too_small;too_small.nodes=1;fails(session.saturate("(add x 0)",too_small),Error::Code::ResourceLimit);
  Session expanding;expanding.define_operator("wrap",1);expanding.define_operator("pair",2);take(expanding.load_rules("(rule grow (wrap ?x) (wrap (pair ?x ?x)))"));
  Limits bounded;bounded.nodes=20;bounded.classes=20;auto limited=take(expanding.saturate("(wrap x)",bounded));CHECK(limited.expression=="(wrap x)"&&limited.limit_reached&&limited.nodes<=20);

  Session declared;
  CHECK(take(declared.load_rules("(rule identity (copy ?x) ?x)\n(operator copy 1)","declarations.tuner"))==1);
  CHECK(take(declared.saturate("(copy payload)")).expression=="payload");
  CHECK(take(declared.define_operator("copy",1))==0);
  fails(declared.define_operator("copy",2),Error::Code::Conflict);
  fails(declared.define_operator("?invalid",1),Error::Code::InvalidArgument);
  auto operators=declared.operators().size(),rules=declared.rules().size();
  fails(declared.load_rules("(operator staged 1) (rule good (staged ?x) ?x) (rule bad (copy ?x) ?missing)"),Error::Code::InvalidArgument);
  CHECK(declared.operators().size()==operators&&declared.rules().size()==rules);
  fails(declared.load_rules("(operator copy 2)"),Error::Code::Conflict);
  fails(declared.load_rules("(operator bad -1)"),Error::Code::InvalidArgument);
  fails(declared.load_rules("(rule bad (unregistered) 0)"),Error::Code::InvalidArgument);
  fails(declared.load_rules_file("/missing/limestone-rule-file.tuner"),Error::Code::NotFound);
  auto diagnostic=declared.load_rules("; header\n\n(rule bad (unknown ?x) ?x)","broken.tuner");
  fails(diagnostic,Error::Code::InvalidArgument);
  CHECK(diagnostic.error().message.find("broken.tuner:3:11:")!=std::string::npos);
  CHECK(declared.rules().front().location.file=="declarations.tuner"&&declared.rules().front().location.line==1);
  auto malformed=parse_term("\n(iadd x 0", "input.term");fails(malformed,Error::Code::Parse);
  CHECK(malformed.error().message.find("input.term:2:10:")!=std::string::npos);
  fails(parse_term("9223372036854775808"),Error::Code::Parse);
  fails(parse_term("-9223372036854775809"),Error::Code::Parse);
  fails(parse_term("12oops"),Error::Code::Parse);
  fails(parse_term("1.5"),Error::Code::Parse);
  fails(parse_term("'x"),Error::Code::Unsupported);
  fails(parse_term("\"unfinished"),Error::Code::Parse);
  CHECK(take(parse_term("-9223372036854775808")).constant==std::numeric_limits<int64_t>::min());
  auto structured=take(parse_term("\n(copy payload)","roundtrip.term"));
  auto roundtrip=take(declared.saturate(structured));
  CHECK(roundtrip.term.op=="payload"&&roundtrip.term.location.file=="roundtrip.term"&&format_term(roundtrip.term)==roundtrip.expression);
  CHECK(roundtrip.classes>0);
  Term invalid{"copy",{},1};fails(declared.saturate(invalid),Error::Code::InvalidArgument);
  Term deep{"leaf"};for(int i=0;i<258;++i)deep=Term{"copy",{},{},{std::move(deep)}};
  fails(declared.saturate(deep),Error::Code::ResourceLimit);
  Limits quiet;quiet.trace=false;CHECK(take(declared.saturate("(copy payload)",quiet)).trace.empty());
  CHECK(take(declared.saturate("(copy payload)",quiet)).derivation_nodes.empty());
  size_t polls=0;Limits interrupted;interrupted.cancelled=[&]{return ++polls==4;};auto preempted=take(declared.saturate("(copy payload)",interrupted));CHECK(preempted.limit_reached&&preempted.expression=="(copy payload)"&&polls==4);

  Session macros;
  take(macros.load_rules("; @unknown(directive) is only a comment\n@define(ZERO, 0)\n(operator add 2)\n(rule zero (add ?x &ZERO()) ?x)","macros.tuner"));
  CHECK(take(macros.saturate("(add x 0)")).expression=="x");
  CHECK(take(macros.saturate("(add x 0)")).trace.front().find("[expanded]")!=std::string::npos);
  CHECK(macros.rules().front().location.file=="macros.tuner"&&macros.rules().front().location.expanded);
  CHECK(macros.rules().front().location.line==4&&macros.rules().front().location.expanded_offset);
  CHECK(macros.rules().front().lhs.arguments[1].location.line==4&&macros.rules().front().lhs.arguments[1].location.column==20);
  auto macro_error=macros.load_rules("@define(LONG, (unknown ?x))\n(rule bad &LONG() ?x)","mapped.tuner");fails(macro_error,Error::Code::InvalidArgument);CHECK(macro_error.error().message.find("mapped.tuner [expanded]:2:11:")!=std::string::npos);
  auto lex_error=macros.load_rules("@define(ZERO, 0)\n(rule bad (add ?x &ZERO()) ?x)\n)","lexical.tuner");fails(lex_error,Error::Code::Parse);CHECK(lex_error.error().message.find("lexical.tuner [expanded]:3:1:")!=std::string::npos);
  auto copied_error=macros.load_rules("@define(LONG, (add (add ?x 0) 0))\n(rule bad &LONG() ?x))","copied.tuner");fails(copied_error,Error::Code::Parse);CHECK(copied_error.error().message.find("copied.tuner [expanded]:2:22:")!=std::string::npos);
  fails(macros.load_rules("(rule missing (add ?x &ZERO()) ?x)"),Error::Code::Parse);
  CHECK(macros.rules().size()==1);
  fails(macros.load_rules("(rule missing (add ?x $UNKNOWN{}) ?x)"),Error::Code::Parse);

  Session guarded;take(guarded.define_operator("add",2));
  fails(guarded.load_rules("(rule positive-zero (add ?x 0) ?x :where (positive ?x))"),Error::Code::Unsupported);
  CHECK(guarded.rules().empty());
  take(guarded.define_predicate("positive",1,[](std::span<const Term> arguments) {
    return Result<bool>::ok(arguments[0].constant&&*arguments[0].constant>0);
  }));
  take(guarded.define_predicate("unit",1,[](std::span<const Term> arguments) {
    return Result<bool>::ok(arguments[0].constant==1);
  }));
  take(guarded.load_rules("(rule positive-zero (add ?x 0) ?x :where (and (positive ?x) (unit 1)))"));
  CHECK(take(guarded.saturate("(add 2 0)")).expression=="2");
  CHECK(take(guarded.saturate("(add -1 0)")).expression=="(add -1 0)");
  CHECK(take(guarded.saturate("(add x 0)")).expression=="(add x 0)");
  fails(guarded.load_rules("(rule unbound (add ?x 0) ?x :where (positive ?missing))"),Error::Code::InvalidArgument);
  fails(guarded.load_rules("(rule arity (add ?x 0) ?x :where (positive ?x ?x))"),Error::Code::InvalidArgument);
  fails(guarded.load_rules("(rule compound (add ?x 0) ?x :where (positive (add ?x 1)))"),Error::Code::Unsupported);
  fails(guarded.load_rules("(rule conjunction (add ?x 0) ?x :where (and))"),Error::Code::Parse);
  fails(guarded.load_rules("(rule cost (add ?x 0) ?x :cost -1)"),Error::Code::Unsupported);
  fails(guarded.define_predicate("positive",1,[](auto){return Result<bool>::ok(true);}),Error::Code::Conflict);
  fails(guarded.define_predicate("empty",0,{}),Error::Code::InvalidArgument);
  take(guarded.define_predicate("failure",0,[](auto){return Result<bool>::err({Error::Code::Conflict,"analysis rejected the match"});}));
  take(guarded.load_rules("(rule rejected (add ?x 1) ?x :where (failure))","analysis.tuner"));
  auto rejected=guarded.saturate("(add 2 1)");fails(rejected,Error::Code::Conflict);
  CHECK(rejected.error().message.find("analysis.tuner:1:1:")!=std::string::npos);

  Session weighted;
  take(weighted.load_rules("(operator imul 2) (operator ishl 2) (rule power (imul ?x 4) (ishl ?x 2))"));
  auto original=take(weighted.saturate("(imul x 4)"));CHECK(original.expression=="(imul x 4)"&&original.cost==5);
  CostModel cheap_shift;cheap_shift.operators={{"imul",20},{"ishl",1}};
  auto shift=take(weighted.saturate("(imul x 4)",{},cheap_shift));CHECK(shift.expression=="(ishl x 2)"&&shift.cost==3);
  for(int i=0;i<5;++i){auto again=take(weighted.saturate("(imul x 4)",{},cheap_shift));CHECK(again.expression==shift.expression&&again.trace==shift.trace);}
  CostModel cheap_multiply;cheap_multiply.operators={{"imul",1},{"ishl",20}};
  CHECK(take(weighted.saturate("(imul x 4)",{},cheap_multiply)).expression=="(imul x 4)");
  CostModel unknown;unknown.operators={{"missing",1}};fails(weighted.saturate("x",{},unknown),Error::Code::InvalidArgument);
  CostModel infinite;infinite.literal=std::numeric_limits<size_t>::max();fails(weighted.saturate("1",{},infinite),Error::Code::InvalidArgument);
  CostModel overflow;overflow.operators={{"imul",std::numeric_limits<size_t>::max()-1},{"ishl",std::numeric_limits<size_t>::max()-1}};
  fails(weighted.saturate("(imul x 4)",{},overflow),Error::Code::ResourceLimit);
  overflow.operators["ishl"]=1;CHECK(take(weighted.saturate("(imul x 4)",{},overflow)).expression=="(ishl x 2)");
  CostModel zero;zero.literal=0;zero.operators={{"imul",0},{"ishl",0}};CHECK(take(weighted.saturate("(imul 1 4)",{},zero)).cost==0);
  auto copied=weighted;take(weighted.define_operator("extra",1));
  CHECK(take(copied.saturate("(imul x 4)",{},cheap_shift)).expression=="(ishl x 2)");
  Session invalidating;take(invalidating.load_rules("(operator wrap 1) (rule leaf (wrap ?x) payload)"));
  CHECK(take(invalidating.saturate("(wrap x)")).expression=="payload");
  take(invalidating.define_operator("payload",1));fails(invalidating.saturate("(wrap x)"),Error::Code::InvalidArgument);
  copied.add_rule(copied.rules().front());fails(copied.saturate("(imul x 4)"),Error::Code::InvalidArgument);
  Limits large_time;large_time.time_ms=std::numeric_limits<uint64_t>::max();
  CHECK(take(weighted.saturate("(imul x 4)",large_time,cheap_shift)).saturated);
});}
