#pragma once
#include "bin2bin.hpp"
namespace limestone::bin2bin::detail {
Result<SemanticRegion> transformed_region(const Architecture&,std::span<const uint8_t>,const TranslationOptions&);
bool compatible_states(const Architecture&,const Architecture&,const TranslationOptions&);
Result<int> translation_contract(const metacode::Value::Object*,EncodingForm&);
Result<std::string> transformed_semantics(const TranslationOptions&,const LiftedInstruction&);
Result<Architecture> load_masked(const metacode::Architecture&,Architecture);
Result<std::vector<Instruction>> decode_native(const Architecture&,std::span<const uint8_t>,uint64_t);
Result<std::vector<uint8_t>> encode_native(const Architecture&,uint32_t,const std::map<std::string,std::string>&,uint64_t);
Result<std::vector<Instruction>> decode_masked(const Architecture&,std::span<const uint8_t>,uint64_t);
Result<std::string> semantics(const Architecture&,const Instruction&);
Result<std::vector<uint8_t>> translate_masked(const Architecture&,const Architecture&,std::span<const uint8_t>,const TranslationOptions&);
}
