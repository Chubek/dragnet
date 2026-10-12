#include "test.hpp"
#include "bin2bin.hpp"
#include "bin2bin/runtime.hpp"
#include "tunah/binary_adapter.hpp"
#include <filesystem>

int main(int argc,char** argv){return test_main([&]{
  using namespace limestone;using namespace bin2bin;CHECK(argc==3);
  auto src=take(from_metacode(take(metacode::load_isa_file(std::string(argv[1])+"/byte-source.isa"))));
  auto dst=take(from_metacode(take(metacode::load_isa_file(std::string(argv[1])+"/byte-target.isa"))));
  std::vector<uint8_t> bytes{1,1,1};
  CHECK(take(translate(src,src,bytes))==bytes);
  auto translated=take(translate(src,dst,bytes));CHECK(translated==std::vector<uint8_t>({9,9,9}));
  CHECK(take(translate(dst,src,translated))==bytes);
  auto instructions=take(decode(src,std::vector<uint8_t>{1,255},16));CHECK(instructions[0].address==16&&instructions[1].status==Status::Unsupported);
  CHECK(disassemble(instructions).find("11: .byte 0xff ; unsupported")!=std::string::npos);
  CHECK(take(lift(src,bytes))[0].semantics=="(set counter (add counter 1))");
  CHECK(take(decompile(src,bytes,16)).find("10: (set counter")!=std::string::npos);
  fails(translate(src,dst,std::vector<uint8_t>{255}),Error::Code::Unsupported);
  fails(decode(src,bytes,std::numeric_limits<uint64_t>::max()),Error::Code::InvalidArgument);
  TranslationOptions overflow;overflow.target_address=UINT64_MAX;fails(translate(src,dst,bytes,nullptr,overflow),Error::Code::InvalidArgument);
  // Numeric opcode coincidence must not imply semantic equivalence.
  auto wrong=src;wrong.name="wrong";wrong.semantics[1]="(set counter (sub counter 1))";
  fails(translate(src,wrong,bytes),Error::Code::Unsupported);
  auto ambiguous=dst;ambiguous.status[9]=Status::Ambiguous;fails(translate(src,ambiguous,bytes),Error::Code::Unsupported);
  auto domain=dst;domain.execution_domain="native_machine_code";fails(translate(src,domain,bytes),Error::Code::Unsupported);
  auto no_semantics=dst;no_semantics.semantics.clear();fails(translate(src,no_semantics,bytes),Error::Code::Unsupported);
  auto malformed=src;malformed.semantics[1]="(unterminated";fails(lift(malformed,bytes),Error::Code::Parse);
  fails(translate(malformed,malformed,bytes),Error::Code::Parse);auto missing=src;missing.semantics.clear();fails(translate(missing,missing,bytes),Error::Code::Unsupported);
  auto control_metadata=take(metacode::load_isa_file(std::string(argv[1])+"/control-byte.isa"));auto terminal=take(from_metacode(control_metadata));auto terminal_bytes=std::vector<uint8_t>{1,2,3,1};auto terminal_stream=take(decode(terminal,terminal_bytes));CHECK(terminal_stream[1].control==ControlFlow::Return&&terminal_stream[2].control==ControlFlow::Trap);auto exits=take(analyze(terminal,terminal_bytes));CHECK(exits.blocks.size()==3&&exits.blocks[0].successors.empty()&&exits.blocks[1].successors.empty());CHECK(take(translate(terminal,terminal,terminal_bytes))==terminal_bytes);
  auto fallthrough=terminal;fallthrough.control.erase(2);fails(translate(terminal,fallthrough,std::vector<uint8_t>{2}),Error::Code::Unsupported);fails(analyze(src,std::vector<uint8_t>{255}),Error::Code::Unsupported);fails(analyze(src,std::vector<uint8_t>{1},UINT64_MAX),Error::Code::InvalidArgument);
  using Object=metacode::Value::Object;auto& contract=std::get<Object>(std::get<Object>(control_metadata.operations[0].fields.at("tooling").data).at("binary_translation").data);
  for(auto [label,classification]:std::vector<std::pair<std::string,Status>>{{"unsupported",Status::Unsupported},{"fallback",Status::Fallback},{"architecture_specific",Status::ArchitectureSpecific},{"privileged",Status::Privileged},{"environment_dependent",Status::EnvironmentDependent},{"ambiguous",Status::Ambiguous}}){contract["status"]=metacode::Value(label);auto classified=take(from_metacode(control_metadata));CHECK(take(decode(classified,std::vector<uint8_t>{1}))[0].status==classification);fails(translate(classified,classified,std::vector<uint8_t>{1}),Error::Code::Unsupported);}
  contract["status"]=metacode::Value(std::string("unknown"));fails(from_metacode(control_metadata),Error::Code::InvalidArgument);contract["status"]=metacode::Value(std::string("supported"));contract["schema_version"]=metacode::Value(uint64_t(2));fails(from_metacode(control_metadata),Error::Code::InvalidArgument);contract["schema_version"]=metacode::Value(uint64_t(1));contract["control_flow"]=metacode::Value(std::string("branch"));fails(from_metacode(control_metadata),Error::Code::Unsupported);contract.erase("control_flow");
  TranslationCache memory;CHECK(take(translate(src,dst,bytes,&memory))==translated);CHECK(memory.entries.size()==1);
  auto changed=dst;changed.version="2";changed.opcodes.erase(9);changed.semantics.erase(9);changed.opcodes[10]="inc2";changed.semantics[10]=src.semantics.at(1);
  CHECK(take(translate(src,changed,bytes,&memory))==std::vector<uint8_t>({10,10,10}));CHECK(memory.entries.size()==2);
  TranslationOptions options;options.rule_version="changed";take(translate(src,dst,bytes,&memory,options));CHECK(memory.entries.size()==3);
  auto rewritten=dst;rewritten.opcodes[9]="subtract-negative-one";rewritten.semantics[9]="(set counter (sub counter -1))";fails(translate(src,rewritten,bytes),Error::Code::Unsupported);
  tunah::Session binary_rules;take(binary_rules.load_rules("(operator set 2) (operator add 2) (operator sub 2) (rule increment (add ?x 1) (sub ?x -1))"));
  size_t validations=0;tunah::BinaryAdapterOptions binary_options;binary_options.costs.operators={{"add",20},{"sub",1}};binary_options.legality=[&](const LiftedInstruction& instruction,const tunah::Term& term){++validations;auto candidate=tunah::format_term(term);return Result<bool>::ok(instruction.status==Status::Supported&&instruction.control==ControlFlow::Fallthrough&&instruction.semantics=="(set counter (add counter 1))"&&(candidate==instruction.semantics||candidate=="(set counter (sub counter -1))"));};
  TranslationOptions optimized;optimized.source_address=100;optimized.semantic_transform=take(tunah::binary_transform(binary_rules,"counter:wrapping-i64:1",binary_options));TranslationCache optimized_cache;CHECK(take(translate(src,rewritten,bytes,&optimized_cache,optimized))==std::vector<uint8_t>({9,9,9})&&validations==6&&optimized_cache.entries.size()==1);take(translate(src,rewritten,bytes,&optimized_cache,optimized));CHECK(validations==6);
  // Independent counter execution validates the rewritten encoding's behavior.
  int64_t source_counter=39,target_counter=39;for(auto& i:take(decode(src,bytes))){CHECK(i.mnemonic=="inc");++source_counter;}for(auto& i:take(decode(rewritten,take(translate(src,rewritten,bytes,nullptr,optimized))))){CHECK(i.mnemonic=="subtract-negative-one");target_counter-= -1;}CHECK(source_counter==42&&target_counter==source_counter);
  auto alternatives=rewritten;alternatives.opcodes[8]="increment";alternatives.semantics[8]=src.semantics.at(1);take(translate(src,alternatives,bytes,&optimized_cache,optimized));auto prior_entries=optimized_cache.entries.size();binary_options.costs.operators={{"add",1},{"sub",20}};auto unoptimized=optimized;unoptimized.semantic_transform=take(tunah::binary_transform(binary_rules,"counter:wrapping-i64:1",binary_options));CHECK(take(translate(src,alternatives,bytes,&optimized_cache,unoptimized))==std::vector<uint8_t>({8,8,8})&&optimized_cache.entries.size()==prior_entries+1);
  auto identity=optimized.semantic_transform->identity;take(binary_rules.load_rules("(rule negative-zero (sub ?x 0) ?x)"));CHECK(take(tunah::binary_transform(binary_rules,"counter:wrapping-i64:1",binary_options)).identity!=identity);CHECK(optimized.semantic_transform->identity==identity);
  binary_options.limits.cancelled=[](){return true;};auto cancellable=optimized;cancellable.semantic_transform=take(tunah::binary_transform(binary_rules,"counter:wrapping-i64:1",binary_options));CHECK(!cancellable.semantic_transform->cacheable);prior_entries=optimized_cache.entries.size();CHECK(take(translate(src,alternatives,bytes,&optimized_cache,cancellable))==std::vector<uint8_t>({8,8,8})&&optimized_cache.entries.size()==prior_entries);
  auto illegal_options=binary_options;illegal_options.limits={};illegal_options.costs.operators={{"add",20},{"sub",1}};illegal_options.legality=[](const LiftedInstruction& instruction,const tunah::Term& term){return Result<bool>::ok(tunah::format_term(term)==instruction.semantics);};auto illegal=optimized;illegal.semantic_transform=take(tunah::binary_transform(binary_rules,"strict:counter:1",illegal_options));TranslationCache rejected_cache;fails(translate(src,rewritten,bytes,&rejected_cache,illegal),Error::Code::Conflict);CHECK(rejected_cache.entries.empty());
  fails(tunah::binary_transform(binary_rules,"",binary_options),Error::Code::InvalidArgument);fails(tunah::binary_transform(binary_rules,"counter",{}),Error::Code::Unsupported);
  auto invalid_transform=optimized;invalid_transform.semantic_transform->identity.clear();fails(translate(src,rewritten,bytes,nullptr,invalid_transform),Error::Code::InvalidArgument);invalid_transform=optimized;invalid_transform.semantic_transform->apply={};fails(translate(src,rewritten,bytes,nullptr,invalid_transform),Error::Code::InvalidArgument);
  auto throwing=optimized;throwing.semantic_transform=SemanticTransform{"throwing",[](const auto&)->Result<std::string>{throw std::runtime_error("failed host analysis");}};fails(translate(src,rewritten,bytes,nullptr,throwing),Error::Code::Internal);
  // A region rule combines sequential architectural updates. Its adapter owns
  // its rules and proves the total wrapping increment at both term boundaries.
  tunah::Session region_rules;take(region_rules.load_rules("(operator seq 2) (operator inc 0) (operator inc2 0) (operator inc3 0) (rule pair (seq (inc) (inc)) (inc2))"));
  tunah::BinaryRegionAdapterOptions region_options;region_options.ingest=[](const SemanticRegion& region)->Result<tunah::Term>{
    std::string term;for(auto it=region.instructions.rbegin();it!=region.instructions.rend();++it){if(it->semantics!="(set counter (add counter 1))")return Result<tunah::Term>::err({Error::Code::Unsupported,"not a counter region"});term=term.empty()?"(inc)":"(seq (inc) "+term+")";}return tunah::parse_term(term);
  };
  std::function<size_t(const tunah::Term&)> increment=[&](const tunah::Term& term)->size_t{
    if(term.op=="seq"&&term.arguments.size()==2)return increment(term.arguments[0])+increment(term.arguments[1]);
    if(!term.arguments.empty())return 100;return term.op=="inc"?1:term.op=="inc2"?2:term.op=="inc3"?3:100;
  };
  size_t region_checks=0;region_options.legality=[&](const SemanticRegion& input,const tunah::Term& term){++region_checks;return Result<bool>::ok(increment(term)==input.instructions.size());};
  region_options.reconstruct=[](const SemanticRegion& input,const tunah::Term& term)->Result<SemanticRegion>{
    SemanticRegion result;std::function<void(const tunah::Term&)> emit=[&](const tunah::Term& t){if(t.op=="seq"){for(auto& child:t.arguments)emit(child);}else{auto n=t.op=="inc"?1:t.op=="inc2"?2:3;result.instructions.push_back({input.instructions.front().address,"(set counter (add counter "+std::to_string(n)+"))",Status::Supported});}};emit(term);
    for(auto [label,index]:input.boundaries)result.boundaries[label]=index==input.instructions.size()?result.instructions.size():std::min(index,result.instructions.size()-1);return Result<SemanticRegion>::ok(std::move(result));
  };
  auto combined_target=dst;combined_target.opcodes[7]="inc2";combined_target.semantics[7]="(set counter (add counter 2))";combined_target.opcodes[6]="inc3";combined_target.semantics[6]="(set counter (add counter 3))";
  TranslationOptions combined;combined.region_transform=take(tunah::binary_region_transform(region_rules,"counter:wrapping-i64:region:1",region_options));TranslationCache region_cache;
  auto combined_bytes=take(translate(src,combined_target,bytes,&region_cache,combined));CHECK(combined_bytes==std::vector<uint8_t>({9,7})&&region_checks==2);take(translate(src,combined_target,bytes,&region_cache,combined));CHECK(region_checks==2);
  int64_t combined_counter=39;for(auto opcode:combined_bytes)combined_counter+=opcode==9?1:opcode==7?2:3;CHECK(combined_counter==42);
  auto frozen_identity=combined.region_transform->identity;take(region_rules.load_rules("(rule triple (seq (inc) (inc2)) (inc3))"));CHECK(take(translate(src,combined_target,bytes,nullptr,combined))==combined_bytes);
  auto triple=combined;triple.region_transform=take(tunah::binary_region_transform(region_rules,"counter:wrapping-i64:region:1",region_options));CHECK(triple.region_transform->identity!=frozen_identity&&take(translate(src,combined_target,bytes,&region_cache,triple))==std::vector<uint8_t>({6})&&region_cache.entries.size()==2);
  auto rejected_region=region_options;rejected_region.legality=[](const SemanticRegion&,const tunah::Term& term){return Result<bool>::ok(term.op=="seq");};auto reject=combined;reject.region_transform=take(tunah::binary_region_transform(region_rules,"counter:strict",rejected_region));fails(translate(src,combined_target,bytes,nullptr,reject),Error::Code::Conflict);
  fails(tunah::binary_region_transform(region_rules,"",region_options),Error::Code::InvalidArgument);fails(tunah::binary_region_transform(region_rules,"counter",{}),Error::Code::InvalidArgument);
  auto adapted_target=combined_target;adapted_target.state_model="host-counter";adapted_target.execution_domain="host-bytecode";fails(translate(src,adapted_target,bytes,nullptr,combined),Error::Code::Unsupported);
  auto adapted=combined;adapted.region_transform->source_state_model=src.state_model;adapted.region_transform->target_state_model=adapted_target.state_model;adapted.region_transform->source_domain=src.execution_domain;adapted.region_transform->target_domain=adapted_target.execution_domain;CHECK(take(translate(src,adapted_target,bytes,nullptr,adapted))==combined_bytes);adapted.region_transform->source_domain="wrong";fails(translate(src,adapted_target,bytes,nullptr,adapted),Error::Code::Unsupported);
  auto unstable_options=region_options;bool region_cancelled=false;unstable_options.limits.cancelled=[&]{return region_cancelled;};RuntimeOptions region_runtime_options;region_runtime_options.translation.region_transform=take(tunah::binary_region_transform(region_rules,"counter:live",unstable_options));Runtime region_runtime(src,combined_target,region_runtime_options);
  CHECK(take(region_runtime.prepare(bytes,100))->bytes==std::vector<uint8_t>({6}));region_cancelled=true;CHECK(take(region_runtime.prepare(bytes,100))->bytes==std::vector<uint8_t>({9,9,9}));
  // Cancellable/time-limited transformations cannot hit resident runtime bytes.
  RuntimeOptions dynamic_options;dynamic_options.hot_threshold=2;dynamic_options.translation=cancellable;
  bool cancel=false;auto dynamic_rules=binary_options;dynamic_rules.costs.operators={{"add",20},{"sub",1}};dynamic_rules.limits.cancelled=[&]{return cancel;};dynamic_options.translation.semantic_transform=take(tunah::binary_transform(binary_rules,"counter:dynamic:1",dynamic_rules));
  size_t dynamic_installs=0;Runtime dynamic(src,alternatives,dynamic_options,[&](const auto& region)->Result<std::function<Result<int64_t>()>>{++dynamic_installs;auto opcode=region.bytes[0];return Result<std::function<Result<int64_t>()>>::ok([opcode]{return Result<int64_t>::ok(opcode);});});
  auto dynamic_cold=take(dynamic.prepare(bytes,100));CHECK(dynamic_cold->bytes==std::vector<uint8_t>({9,9,9})&&!dynamic_cold->compiled());
  auto dynamic_hot=take(dynamic.prepare(bytes,100));CHECK(take(dynamic.invoke(dynamic_hot))==9&&dynamic_installs==1);cancel=true;
  auto dynamic_cancelled=take(dynamic.prepare(bytes,100));CHECK(dynamic_cancelled->bytes==std::vector<uint8_t>({8,8,8})&&take(dynamic.invoke(dynamic_cancelled))==8&&dynamic_installs==2&&dynamic_cold->bytes[0]==9&&dynamic_hot->valid());
  CHECK(take(dynamic.invalidate(100,1))==3&&!dynamic_hot->valid()&&!dynamic_cancelled->valid());
  // Reentrant transformation must neither recurse nor publish invalidated code.
  Runtime* translating=nullptr;bool invalidate_during_translation=true;RuntimeOptions reentrant_options;
  reentrant_options.translation.semantic_transform=SemanticTransform{"reentrant",[&](const LiftedInstruction& instruction){fails(translating->prepare(bytes,100),Error::Code::Conflict);fails(translating->open_persistent_cache("unused"),Error::Code::Conflict);if(invalidate_during_translation)take(translating->invalidate(100,1));return Result<std::string>::ok(instruction.semantics);},false};
  Runtime reentrant_runtime(src,dst,reentrant_options);translating=&reentrant_runtime;fails(reentrant_runtime.prepare(bytes,100),Error::Code::Interrupted);CHECK(!reentrant_runtime.resident_regions());invalidate_during_translation=false;CHECK(take(reentrant_runtime.prepare(bytes,100))->valid());
  auto masked=take(from_metacode(take(metacode::load_isa_file(std::string(argv[1])+"/masked-source.isa"))));
  auto wider=take(from_metacode(take(metacode::load_isa_file(std::string(argv[1])+"/masked-target.isa"))));
  auto mixed_overflow=overflow;mixed_overflow.source_address=UINT64_MAX;mixed_overflow.target_address=0;fails(translate(src,wider,std::vector<uint8_t>{1},nullptr,mixed_overflow),Error::Code::InvalidArgument);
  auto add=take(encode(masked,"add",{{"dst","r1"},{"src","r2"},{"imm","-8"}},100));CHECK(add==std::vector<uint8_t>({1,137}));
  auto decoded=take(decode(masked,add,100));CHECK(decoded[0].operands.at("imm")=="-8"&&decoded[0].operands.at("src")=="r2");
  CHECK(take(lift(masked,add,100))[0].semantics=="(set r1 (add r2 -8))");
  auto big=masked;big.endianness="big";auto big_bytes=take(encode(big,"add",decoded[0].operands));CHECK(big_bytes==std::vector<uint8_t>({137,1}));CHECK(take(decode(big,big_bytes))[0].operands==decoded[0].operands);
  fails(encode(masked,"add",{{"dst","r1"},{"src","r2"},{"imm","8"}}),Error::Code::InvalidArgument);
  fails(decode(masked,std::vector<uint8_t>{1}),Error::Code::Parse);fails(decode(masked,std::vector<uint8_t>{255,255}),Error::Code::Unsupported);
  auto overlapping=masked;overlapping.forms.push_back(overlapping.forms.front());overlapping.forms.back().id=100;fails(validate(overlapping),Error::Code::Conflict);
  auto bad_fields=masked;bad_fields.forms[0].fields[0].lsb=0;fails(validate(bad_fields),Error::Code::InvalidArgument);
  auto trapped=masked;for(auto& form:trapped.forms)if(form.mnemonic=="ret"){form.control=ControlFlow::Trap;form.semantics="(trap)";}auto trap_bytes=take(encode(trapped,"ret"));CHECK(take(decode(trapped,trap_bytes))[0].control==ControlFlow::Trap&&take(analyze(trapped,trap_bytes)).blocks[0].successors.empty());CHECK(take(translate(trapped,trapped,trap_bytes))==trap_bytes);
  std::vector<uint8_t> control=add;auto branch=take(encode(masked,"branch",{{"target","106"}},102));control.insert(control.end(),branch.begin(),branch.end());control.insert(control.end(),add.begin(),add.end());auto ret=take(encode(masked,"ret"));control.insert(control.end(),ret.begin(),ret.end());
  auto cfg=take(analyze(masked,control,100));CHECK(cfg.blocks.size()==3&&cfg.blocks[0].successors==std::vector<uint64_t>({104,106}));
  TranslationOptions relocated;relocated.source_address=100;relocated.target_address=500;
  auto migrated=take(translate(masked,wider,control,nullptr,relocated));CHECK(migrated.size()==12);auto target_stream=take(decode(wider,migrated,500));CHECK(target_stream[1].branch_target==509);
  auto reverse=relocated;std::swap(reverse.source_address,reverse.target_address);CHECK(take(translate(wider,masked,migrated,nullptr,reverse))==control);
  CHECK(take(translate(masked,masked,control,nullptr,relocated)).size()==control.size());
  auto invalid_target=control;invalid_target[3]=1;fails(analyze(masked,invalid_target,100),Error::Code::Conflict);fails(translate(masked,wider,invalid_target,nullptr,relocated),Error::Code::Conflict);
  auto redirect=relocated;redirect.semantic_transform=SemanticTransform{"illegal-target-edit",[](const LiftedInstruction& instruction){if(instruction.branch_target)return Result<std::string>::ok("(branch_if flag "+std::to_string(*instruction.branch_target+1)+")");return Result<std::string>::ok(instruction.semantics);}};fails(translate(masked,wider,control,nullptr,redirect),Error::Code::Unsupported);
  auto passthrough=relocated;passthrough.semantic_transform=SemanticTransform{"identity",[](const LiftedInstruction& instruction){CHECK(instruction.address>=100);return Result<std::string>::ok(instruction.semantics);}};CHECK(take(translate(masked,wider,control,nullptr,passthrough))==migrated);
  auto noop=take(encode(masked,"add",{{"dst","r1"},{"src","r1"},{"imm","0"}},104));auto removable=control;std::copy(noop.begin(),noop.end(),removable.begin()+4);
  auto removed=relocated;removed.region_transform=RegionTransform{"remove-proved-noop",[](const SemanticRegion& input){auto output=input;CHECK(output.instructions[2].semantics=="(set r1 (add r1 0))");output.instructions.erase(output.instructions.begin()+2);for(auto& [label,index]:output.boundaries)if(index>2)--index;return Result<SemanticRegion>::ok(std::move(output));}};
  auto removed_bytes=take(translate(masked,wider,removable,nullptr,removed));auto removed_stream=take(decode(wider,removed_bytes,500));CHECK(removed_bytes.size()==9&&removed_stream[1].branch_target==506&&removed_stream[2].control==ControlFlow::Return);
  for(unsigned mistake=0;mistake<6;++mistake){auto bad_region=removed;bad_region.region_transform->identity="bad-region:"+std::to_string(mistake);bad_region.region_transform->apply=[mistake](const SemanticRegion& input){auto output=input;if(mistake==0)output.boundaries.erase(106);if(mistake==1)output.boundaries[100]=1;if(mistake==2)output.boundaries[108]=1;if(mistake==3)output.boundaries[104]=100;if(mistake==4)output.instructions[1].branch_target.reset();if(mistake==5)output.instructions[0].branch_target=106;return Result<SemanticRegion>::ok(std::move(output));};fails(translate(masked,wider,removable,nullptr,bad_region),Error::Code::Conflict);}
  // Malformed source targets are rejected before calling a reconstruction proof.
  size_t region_calls=0;auto source_check=removed;source_check.region_transform->apply=[&](const SemanticRegion& input){++region_calls;return Result<SemanticRegion>::ok(input);};fails(translate(masked,wider,invalid_target,nullptr,source_check),Error::Code::Conflict);CHECK(region_calls==0);
  auto relaxed=wider;auto long_branch=std::find_if(relaxed.forms.begin(),relaxed.forms.end(),[](auto& f){return f.mnemonic=="jump";});
  CHECK(long_branch!=relaxed.forms.end());auto short_branch=*long_branch;short_branch.id=100;short_branch.width=8;short_branch.mask=15;short_branch.base=4;short_branch.fields[0].lsb=4;short_branch.fields[0].width=4;relaxed.forms.push_back(short_branch);take(validate(relaxed));
  auto distant=take(encode(masked,"branch",{{"target","110"}},100));for(unsigned k=0;k<4;++k)distant.insert(distant.end(),add.begin(),add.end());distant.insert(distant.end(),ret.begin(),ret.end());
  relocated.target_address=500;auto widened=take(translate(masked,relaxed,distant,nullptr,relocated));auto widened_stream=take(decode(relaxed,widened,500));CHECK(widened_stream.front().bytes.size()==3&&widened_stream.front().branch_target==515);
  take(translate(masked,wider,control,&memory,relocated));auto old_size=memory.entries.size();relocated.target_address=1000;take(translate(masked,wider,control,&memory,relocated));CHECK(memory.entries.size()==old_size+1);
  RuntimeOptions runtime_options;runtime_options.hot_threshold=2;runtime_options.max_regions=1;size_t installs=0;
  Runtime runtime(src,dst,runtime_options,[&](const TranslatedRegion& region)->Result<std::function<Result<int64_t>()>> {++installs;auto length=region.bytes.size();return Result<std::function<Result<int64_t>()>>::ok([length]{return Result<int64_t>::ok(static_cast<int64_t>(length));});});
  auto cold=take(runtime.prepare(bytes,100));CHECK(!cold->compiled());fails(runtime.invoke(cold),Error::Code::Unsupported);
  auto hot=take(runtime.prepare(bytes,100));CHECK(hot->compiled()&&!cold->compiled()&&take(runtime.invoke(hot))==3&&installs==1);
  take(runtime.prepare(bytes,100));CHECK(installs==1);take(runtime.prepare(bytes,200));CHECK(runtime.resident_regions()==1);CHECK(take(runtime.invoke(hot))==3);
  CHECK(take(runtime.invalidate(101,1))==2&&!cold->valid()&&!hot->valid());fails(runtime.invoke(hot),Error::Code::Conflict);
  CHECK(!take(runtime.prepare(bytes,100))->compiled());Runtime other(src,dst);fails(other.invoke(hot),Error::Code::InvalidArgument);
  fails(runtime.invalidate(UINT64_MAX,2),Error::Code::InvalidArgument);fails(runtime.prepare({},100),Error::Code::InvalidArgument);
  Runtime* reentered=nullptr;RuntimeOptions eager;eager.hot_threshold=1;
  Runtime installer_error(src,dst,eager,[](const auto&)->Result<std::function<Result<int64_t>()>>{throw Error{Error::Code::Unsupported,"installer unavailable"};});fails(installer_error.prepare(bytes,100),Error::Code::Unsupported);
  Runtime execution_error(src,dst,eager,[](const auto&)->Result<std::function<Result<int64_t>()>>{return Result<std::function<Result<int64_t>()>>::ok([]()->Result<int64_t>{throw Error{Error::Code::Interrupted,"guest interrupted"};});});fails(execution_error.invoke(take(execution_error.prepare(bytes,100))),Error::Code::Interrupted);
  Runtime interrupted(src,dst,eager,[&](const auto& region)->Result<std::function<Result<int64_t>()>>{take(reentered->invalidate(region.guest_address,1));return Result<std::function<Result<int64_t>()>>::ok([]{return Result<int64_t>::ok(0);});});reentered=&interrupted;
  fails(interrupted.prepare(bytes,100),Error::Code::Interrupted);CHECK(interrupted.resident_regions()==0);
  // Reentrant invalidation may drop the caller's last executable handle while
  // the callable is running; the invocation itself must retain its code owner.
  struct CodeOwner{bool* alive;explicit CodeOwner(bool& state):alive(&state){state=true;}~CodeOwner(){*alive=false;}};
  bool owner_alive=false;Runtime* executing=nullptr;std::shared_ptr<const TranslatedRegion> active;
  Runtime owning(src,dst,eager,[&](const auto&)->Result<std::function<Result<int64_t>()>>{auto owner=std::make_shared<CodeOwner>(owner_alive);return Result<std::function<Result<int64_t>()>>::ok([&,owner]{take(executing->invalidate(100,1));active.reset();CHECK(owner_alive);return Result<int64_t>::ok(42);});});executing=&owning;
  active=take(owning.prepare(bytes,100));CHECK(owner_alive&&take(owning.invoke(active))==42&&!active&&!owner_alive&&owning.resident_regions()==0);
  std::shared_ptr<const TranslatedRegion> expired;{Runtime ephemeral(src,dst);expired=take(ephemeral.prepare(bytes,200));CHECK(expired->valid());}CHECK(!expired->valid());
#ifdef LIMESTONE_TEST_LMDB
  std::filesystem::remove_all(argv[2]);
  {TranslationCache persistent;take(open_cache(persistent,argv[2]));CHECK(take(translate(src,dst,bytes,&persistent))==translated);}
  {TranslationCache persistent;take(open_cache(persistent,argv[2]));CHECK(take(translate(src,dst,bytes,&persistent))==translated);CHECK(persistent.entries.size()==1);CHECK(take(translate(src,changed,bytes,&persistent))==std::vector<uint8_t>({10,10,10}));}
  std::filesystem::remove_all(argv[2]);
#else
  TranslationCache persistent;fails(open_cache(persistent,argv[2]),Error::Code::Unsupported);
#endif
});}
