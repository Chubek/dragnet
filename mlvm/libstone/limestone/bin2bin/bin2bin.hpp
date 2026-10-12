#pragma once
#include "../limestone/foundation.hpp"
#include "../metacode/metacode.hpp"
#include <map>
namespace limestone::bin2bin {
enum class Status { Supported, Unsupported, Fallback, ArchitectureSpecific, Privileged, EnvironmentDependent, Ambiguous };
enum class ControlFlow { Fallthrough, Branch, ConditionalBranch, Call, Return, IndirectBranch, Trap };
enum class OperandKind { Unsigned, Signed, Register, PCRelative };
struct EncodingField {
  std::string name; uint32_t lsb=0, width=0;
  OperandKind kind=OperandKind::Unsigned;
  std::string register_class;
  uint32_t scale=1;
  bool relative_to_end=true;
};
struct EncodingForm {
  uint32_t id=0; std::string mnemonic; uint32_t width=0;
  uint64_t mask=0, base=0;
  std::vector<EncodingField> fields;
  std::string semantics;
  Status status=Status::Supported;
  ControlFlow control=ControlFlow::Fallthrough;
  std::string target_operand, origin;
};
struct Instruction {
  uint64_t address; uint8_t opcode; std::vector<uint8_t> bytes; std::string mnemonic; Status status=Status::Supported;
  std::optional<uint32_t> encoding_id;
  std::map<std::string,std::string> operands;
  ControlFlow control=ControlFlow::Fallthrough;
  std::optional<uint64_t> branch_target;
};
struct DecodedOperands { uint32_t form; std::map<std::string,std::string> operands; };
// Native encodings have target-declared form lengths and semantic operands. The
// adapter owns prefix/ModRM/scattered-field logic; the framework checks exact
// lengths, operand legality, metadata classifications and byte round trips.
struct CodecAdapter {
  std::string identity;
  size_t max_instruction_bytes=4096;
  bool cacheable=true;
  std::function<Result<DecodedOperands>(std::span<const uint8_t>,uint64_t)> decode_one;
  std::function<Result<std::vector<uint8_t>>(uint32_t,const std::map<std::string,std::string>&,uint64_t)> encode_form;
};
// Explicit fixed8, masked and owning native codecs share this contract.
struct Architecture {
  std::string name; std::unordered_map<uint8_t,std::string> opcodes;
  std::unordered_map<uint8_t,std::string> semantics;
  std::unordered_map<uint8_t,Status> status;
  std::string version, description, infobank_version, execution_domain, state_model;
  std::vector<EncodingForm> forms;
  // Required explicitly by the masked codec; legacy fixed8 is endian-independent.
  std::string endianness;
  std::map<std::string,std::map<uint64_t,std::string>> registers;
  // Operand-free byte opcodes may still return, trap, or branch indirectly.
  std::unordered_map<uint8_t,ControlFlow> control;
  std::shared_ptr<const CodecAdapter> codec;
};
struct TranslationRule { std::string source, target; std::vector<uint8_t> bytes; };
struct CacheStorage;
struct TranslationCache {
  std::unordered_map<std::string,std::vector<uint8_t>> entries;
  std::shared_ptr<CacheStorage> storage;
};
struct LiftedInstruction {
  uint64_t address; std::string semantics; Status status;
  ControlFlow control=ControlFlow::Fallthrough; std::optional<uint64_t> branch_target;
};
// Host adapters supply proved equivalent semantics. Identity must cover rules,
// analyses, costs and configuration; nondeterministic adapters bypass byte caches.
struct SemanticTransform {
  std::string identity;
  std::function<Result<std::string>(const LiftedInstruction&)> apply;
  bool cacheable=true;
};
struct SemanticRegion {
  std::vector<LiftedInstruction> instructions;
  // Source/synthetic label -> output instruction index, including one-past-end.
  std::map<uint64_t,size_t> boundaries;
};
struct RegionTransform {
  std::string identity;
  std::function<Result<SemanticRegion>(const SemanticRegion&)> apply;
  bool cacheable=true;
  // Optional, explicit proof context for a state/ABI-changing transformation.
  // All four fields must match the actual source and destination contracts.
  std::string source_state_model, target_state_model, source_domain, target_domain;
};
struct TranslationOptions {
  std::string rule_version, optimization_configuration, translator_configuration, plugin_versions, runtime_configuration;
  uint64_t source_address=0, target_address=0;
  std::optional<SemanticTransform> semantic_transform;
  std::optional<RegionTransform> region_transform;
};
Result<Architecture> from_metacode(const metacode::Architecture&);
// Native codec identity must match tooling.bin2bin.codec_adapter. The callbacks
// and their captured owners are copied; the source metadata may be destroyed.
Result<Architecture> from_metacode(const metacode::Architecture&,const CodecAdapter&);
Result<int> validate(const Architecture&);
// Operand strings are semantic register names or decimal integers. No raw handles.
Result<std::vector<uint8_t>> encode(const Architecture&,std::string_view mnemonic,const std::map<std::string,std::string>& operands={},uint64_t address=0);
// Encode a particular legal form after a layout/relaxation pass chose it.
Result<std::vector<uint8_t>> encode_form(const Architecture&,uint32_t form,const std::map<std::string,std::string>& operands,uint64_t address=0);
struct BasicBlock { uint64_t address; std::vector<size_t> instructions; std::vector<uint64_t> successors; bool indirect_exit=false; };
struct ControlFlowGraph { std::vector<Instruction> instructions; std::vector<BasicBlock> blocks; };
Result<ControlFlowGraph> analyze(const Architecture&,std::span<const uint8_t>,uint64_t address=0);
// Open an LMDB environment directory; failures never downgrade to memory caching.
Result<int> open_cache(TranslationCache&, const std::string& path, size_t map_size=64*1024*1024);
Result<std::vector<Instruction>> decode(const Architecture&,std::span<const uint8_t> bytes,uint64_t address=0);
Result<std::vector<LiftedInstruction>> lift(const Architecture&,std::span<const uint8_t>,uint64_t address=0);
Result<std::vector<uint8_t>> translate(const Architecture&,const Architecture&,std::span<const uint8_t>,TranslationCache* cache=nullptr,const TranslationOptions& = {});
std::string disassemble(const std::vector<Instruction>&);
Result<std::string> decompile(const Architecture&,std::span<const uint8_t>,uint64_t address=0);
}
