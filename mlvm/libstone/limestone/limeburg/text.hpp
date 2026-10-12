#pragma once
#include "limeburg.hpp"
#include "parsers/source.hpp"
#include <map>

namespace limestone::limeburg {
struct InputTree { std::string name; std::vector<Node> nodes; NodeId root; std::string nonterminal; };
struct RuleDocument {
  std::string name;
  RuleSet rules;
  std::map<std::string,uint32_t> terminals;
  std::vector<InputTree> trees;
};
// Text loading performs symbol and type/range validation independently of BURS.
Result<RuleDocument> load_rules(std::string_view,std::string_view file="<limeburg>");
Result<RuleDocument> load_rules(std::string_view,std::string_view file,const syntax::IncludeOptions&);
Result<RuleDocument> load_rules_file(const std::string&);
Result<RuleDocument> load_rules_file(const std::string&,const syntax::IncludeOptions&);
Result<std::string> print_rules(const RuleDocument&);
}
