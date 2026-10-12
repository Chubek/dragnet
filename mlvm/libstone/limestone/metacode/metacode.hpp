#pragma once
#include "../limestone/foundation.hpp"
namespace limestone::metacode {
struct SourceLocation { std::string file; size_t offset=0; uint32_t line=1, column=1; };
struct Value {
  using Object=std::unordered_map<std::string,Value>;
  using Array=std::vector<Value>;
  std::variant<std::string,int64_t,bool,Object,Array,uint64_t> data;
  SourceLocation source;
  Value() : data(std::string{}) {}
  explicit Value(std::string s):data(std::move(s)){}
  explicit Value(int64_t n):data(n){}
  explicit Value(bool b):data(b){}
  explicit Value(Object o):data(std::move(o)){}
  explicit Value(Array a):data(std::move(a)){}
  explicit Value(uint64_t n):data(n){}
  std::string text() const;
};
struct Register { std::string name, klass; uint32_t width=0, number=0; };
struct Operation { std::string name, semantics; Value::Object fields; SourceLocation source; };
struct Architecture {
  std::string name, family, model, version;
  std::unordered_map<std::string,Value> fields;
  std::vector<Register> registers;
  std::vector<Operation> operations;
  std::unordered_map<std::string,Value::Object> encodings;
  std::unordered_map<std::string,std::string> aliases;
  SourceLocation source;
};
Result<Architecture> parse_isa(std::string_view text, std::string_view file="<memory>");
Result<Architecture> load_isa_file(const std::string& path);
std::string dump(const Architecture&);
}
