#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <memory>
#include <span>
#include <functional>
#include <unordered_set>
#include <tuple>
#include <stdexcept>
#include <algorithm>

namespace limestone {
struct Error {
  enum class Code { InvalidArgument, Parse, NotFound, Conflict, Unsupported, Unsatisfiable, Internal, Timeout, Interrupted, ResourceLimit };
  Code code;
  std::string message;
};
template<class T> class Result {
  std::optional<T> value_;
  std::optional<Error> error_;
public:
  static Result ok(T v) { Result r; r.value_=std::move(v); return r; }
  static Result err(Error e) { Result r; r.error_=std::move(e); return r; }
  bool has_value() const { return value_.has_value(); }
  explicit operator bool() const { return has_value(); }
  T& value() { if(!value_) throw std::logic_error("Result has no value"); return *value_; }
  const T& value() const { if(!value_) throw std::logic_error("Result has no value"); return *value_; }
  const Error& error() const { if (!error_) throw std::logic_error("Result has no error"); return *error_; }
};
}
