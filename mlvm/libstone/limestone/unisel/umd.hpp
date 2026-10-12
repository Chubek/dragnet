#pragma once
#include "unisel.hpp"
#include "metacode/metacode.hpp"
#include "parsers/source.hpp"

namespace limestone::unisel {
// Instruction and architecture metadata stay source-located, including unknown fields.
struct MachineDescription {
  std::string name;
  std::vector<Pattern> patterns;
  std::unordered_map<std::string,size_t> operators;
  std::unordered_map<std::string,std::vector<uint32_t>> register_classes;
  std::unordered_map<std::string,uint32_t> physical_names;
  std::vector<std::pair<uint32_t,uint32_t>> aliases;
  std::unordered_map<std::string,metacode::Value::Object> instructions;
  metacode::Value::Object metadata;
  metacode::SourceLocation source;
};
struct Document { MachineDescription machine; std::optional<Program> program; };
Result<Document> load_umd(std::string_view, std::string_view file="<umd>");
Result<Document> load_umd(std::string_view,std::string_view file,const syntax::IncludeOptions&);
// File loaders resolve includes relative to the including file by default. A
// supplied resolver replaces filesystem access, including the root file read.
Result<Document> load_umd_file(const std::string&);
Result<Document> load_umd_file(const std::string&,const syntax::IncludeOptions&);
// Preserve all ISA facts. Only explicit selection_tree contracts become patterns;
// generic opcode classifications are not proof of an instruction's semantics.
Result<MachineDescription> from_metacode(const metacode::Architecture&);
Result<int> validate(const MachineDescription&);
std::string print_umd(const MachineDescription&);
}
