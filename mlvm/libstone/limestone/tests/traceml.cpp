#include "test.hpp"
#include "traceml.hpp"

int main(){return test_main([]{
  using namespace limestone;
  auto eval=[](std::string_view source){return take(traceml::evaluate(take(traceml::compile(source))));};
  CHECK(eval("(add 20 22)")==42);CHECK(eval("(mul -3 -7)")==21);CHECK(eval("0")==0);
  CHECK(eval("(((lambda x (lambda y (add x y))) 10) 7)")==17);
  CHECK(eval("((lambda x ((lambda x x) 5)) 7)")==5);
  CHECK(eval("((lambda f (f 20 22)) add)")==42);
  CHECK(eval("((lambda x 42) (add 9223372036854775807 1))")==42);
  CHECK(eval("(if 0 (add 9223372036854775807 1) 8)")==8);
  CHECK(eval("((if 1 (lambda x (add x 1)) (lambda x 0)) 9)")==10);
  CHECK(eval("(begin (lambda x x) (add 2 3))")==5);
  CHECK(eval("; comment\n (add 1 2)\n 9")==9);
  CHECK(eval("(sub -9223372036854775808 0)")==std::numeric_limits<int64_t>::min());
  for(auto source:{"(add 9223372036854775807 1)","(sub -9223372036854775808 1)","(mul -9223372036854775808 -1)","(neg -9223372036854775808)","(1 2)","(add (lambda x x) 1)"})fails(traceml::evaluate(take(traceml::compile(source))),Error::Code::InvalidArgument);
  for(auto source:{"(lambda 1 2)","(add 1)","(if 1 2)","(begin)","unknown"})fails(traceml::compile(source),Error::Code::InvalidArgument);
  for(auto source:{"", "()", "(add 1 2", "9223372036854775808", ")"})fails(traceml::compile(source),Error::Code::Parse);
  for(const auto& source:{std::string("42")+'\0'+"unknown",std::string("; comment")+'\0'+"\n42",std::string("(add 1 ")+'\0'+"2)"}) {
    fails(traceml::parse(source),Error::Code::Parse);
    fails(traceml::compile(source),Error::Code::Parse);
  }
  auto located=take(traceml::parse("\n  (add 1 2)"));CHECK(located->line==2&&located->column==3);
  auto divergence=take(traceml::compile("((lambda x (x x)) (lambda x (x x)))"));fails(traceml::evaluate(divergence),Error::Code::ResourceLimit);
  fails(traceml::evaluate(take(traceml::compile("1")),0),Error::Code::ResourceLimit);
  fails(traceml::lower_checked(take(traceml::compile("(lambda x x)"))),Error::Code::Unsupported);
  CHECK(take(traceml::lower_checked(take(traceml::compile("(add 1 2)")))).find("const.i64 3")!=std::string::npos);
  auto cyclic=std::make_shared<traceml::Expr>();cyclic->kind=traceml::Expr::Kind::Begin;cyclic->children={cyclic};
  fails(traceml::verify({{cyclic}}),Error::Code::InvalidArgument);cyclic->children.clear();
  auto traced=take(traceml::compile("((lambda x (if (lt x 0) (neg x) (add x 2))) -3)"));
  auto execution=take(traceml::execute(traced));CHECK(execution.value==3&&execution.result_value&&execution.steps>0);
  CHECK(execution.trace==take(traceml::execute(traced)).trace);CHECK(traceml::print_trace(execution).find("taken")!=std::string::npos);
  auto lowering=take(traceml::lower_trace(execution));CHECK(std::count_if(lowering.program.nodes.begin(),lowering.program.nodes.end(),[](auto& n){return n.op=="guard_nonzero";})==1);
  CHECK(std::count_if(lowering.program.nodes.begin(),lowering.program.nodes.end(),[](auto& n){return n.op=="add";})==0);
  auto false_path=take(traceml::execute(take(traceml::compile("(if 0 100 (add 2 3))"))));CHECK(false_path.value==5);take(traceml::lower_trace(false_path));
  auto corrupt=execution;for(auto& event:corrupt.trace)if(event.kind==traceml::TraceEvent::Kind::Primitive){event.result=1000;break;}fails(traceml::lower_trace(corrupt),Error::Code::Conflict);
  traceml::ExecutionOptions trace_options;trace_options.event_limit=1;fails(traceml::execute(traced,trace_options),Error::Code::ResourceLimit);
  trace_options.event_limit=100;trace_options.cancelled=[] {return true;};fails(traceml::execute(traced,trace_options),Error::Code::ResourceLimit);
  trace_options.cancelled={};trace_options.record_trace=false;size_t observed=0;trace_options.observer=[&](auto&){++observed;};CHECK(take(traceml::execute(traced,trace_options)).trace.empty()&&observed>0);
  auto lazy_trace=take(traceml::execute(take(traceml::compile("((lambda x 42) (add 9223372036854775807 1))"))));CHECK(lazy_trace.value==42);CHECK(std::none_of(lazy_trace.trace.begin(),lazy_trace.trace.end(),[](auto& e){return e.kind==traceml::TraceEvent::Kind::Primitive;}));
  using traceml::RuntimeValue;
  auto closures=take(traceml::compile("(lambda x (lambda y (add x y)))"));auto runtime=take(traceml::lower_runtime(closures));closures.forms[0]->children[0]->children[0]->children[0]->atom="mul";
  std::vector<RuntimeValue> ten{RuntimeValue::integer(10)},thirtytwo{RuntimeValue::integer(32)};
  auto closure=take(traceml::run_runtime(runtime,ten)).value;CHECK(closure.callable()&&!closure.integer());runtime={};closures={};CHECK(take(traceml::apply_runtime(closure,thirtytwo)).value.integer()==42);
  auto lazy_runtime=take(traceml::lower_runtime(take(traceml::compile("(lambda x (lambda y x))"))));auto captured=take(traceml::run_runtime(lazy_runtime,ten)).value;auto function_arg=take(traceml::run_runtime(take(traceml::lower_runtime(take(traceml::compile("(lambda z z)")))))).value;
  std::vector<RuntimeValue> functions{function_arg};CHECK(take(traceml::apply_runtime(captured,functions)).value.integer()==10);
  auto guarded=take(traceml::lower_runtime(take(traceml::compile("(lambda x (add 40 (if (lt x 0) (neg x) x)))"))));traceml::ExecutionOptions guard_options;guard_options.record_guards=true;
  std::vector<RuntimeValue> negative{RuntimeValue::integer(-2)};auto guarded_run=take(traceml::run_runtime(guarded,negative,guard_options));CHECK(guarded_run.value.integer()==42&&guarded_run.guards.size()==1&&guarded_run.guards[0].expected());guarded={};
  auto resumed=take(traceml::resume_guard(guarded_run.guards[0],1,guard_options));CHECK(resumed.value.integer()==42);take(traceml::lower_trace(resumed.execution));CHECK(take(traceml::resume_guard(guarded_run.guards[0],0)).value.integer()==38);
  auto pending=take(traceml::run_runtime(take(traceml::lower_runtime(take(traceml::compile("((if 1 (lambda x (add x 1)) (lambda x 0)) 41)")))),{},guard_options));CHECK(pending.value.integer()==42&&take(traceml::resume_guard(pending.guards[0],1)).value.integer()==42&&take(traceml::resume_guard(pending.guards[0],0)).value.integer()==0);
  auto sequence=take(traceml::run_runtime(take(traceml::lower_runtime(take(traceml::compile("(begin (if 1 10 20) 30) 42")))),{},guard_options));CHECK(sequence.guards.size()==1&&take(traceml::resume_guard(sequence.guards[0],0)).value.integer()==42);
  fails(traceml::run_runtime({}),Error::Code::InvalidArgument);fails(traceml::apply_runtime({},ten),Error::Code::InvalidArgument);fails(traceml::apply_runtime(RuntimeValue::integer(1),ten),Error::Code::InvalidArgument);fails(traceml::resume_guard({},0),Error::Code::InvalidArgument);
  fails(traceml::lower_runtime(take(traceml::compile("(add 1 2)")),1),Error::Code::ResourceLimit);guard_options.step_limit=0;fails(traceml::resume_guard(guarded_run.guards[0],1,guard_options),Error::Code::ResourceLimit);
});}
