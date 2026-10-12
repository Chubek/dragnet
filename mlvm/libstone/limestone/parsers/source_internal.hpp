#pragma once
#include "source.hpp"

namespace limestone::syntax::detail {
// Parsers splice owning AST declarations rather than concatenate source text,
// preserving the original file and coordinates of every included declaration.
class IncludeContext {
  const IncludeOptions options_;
  size_t bytes_=0, documents_=0;
  std::vector<std::string> active_;
 public:
  explicit IncludeContext(const IncludeOptions& options):options_(options) {}
  void enter(std::string_view text,std::string_view name) {
    if(name.empty()||name.find('\0')!=name.npos)throw Error{Error::Code::InvalidArgument,"source document needs a nonempty, NUL-free identity"};
    if(std::find(active_.begin(),active_.end(),name)!=active_.end())throw Error{Error::Code::Conflict,"include cycle at "+std::string(name)};
    if(documents_>=options_.documents||active_.size()>options_.depth||text.size()>options_.bytes-bytes_)throw Error{Error::Code::ResourceLimit,"cumulative source/include limit exceeded at "+std::string(name)};
    active_.emplace_back(name);bytes_+=text.size();++documents_;
  }
  void leave()noexcept{active_.pop_back();}
  ResolvedSource resolve(std::string_view including,std::string_view requested) {
    if(!options_.resolver)throw Error{Error::Code::Unsupported,"includes require an explicit source resolver or file loader"};
    if(requested.empty()||requested.find('\0')!=requested.npos)throw Error{Error::Code::InvalidArgument,"invalid include path"};
    if(documents_>=options_.documents||active_.size()>options_.depth)throw Error{Error::Code::ResourceLimit,"source/include limit exceeded before resolving "+std::string(requested)};
    try {
      auto source=options_.resolver(including,requested);if(!source)throw source.error();
      if(source.value().name.empty()||source.value().name.find('\0')!=source.value().name.npos)throw Error{Error::Code::InvalidArgument,"include resolver returned an invalid document identity"};
      return std::move(source.value());
    }catch(const Error&){throw;}
    catch(const std::exception& e){throw Error{Error::Code::Internal,std::string("include resolver: ")+e.what()};}
    catch(...){throw Error{Error::Code::Internal,"include resolver exception"};}
  }
};
struct IncludeScope {
  IncludeContext& context;
  IncludeScope(IncludeContext& c,std::string_view text,std::string_view file):context(c){context.enter(text,file);}
  ~IncludeScope(){context.leave();}
  IncludeScope(const IncludeScope&)=delete;
  IncludeScope& operator=(const IncludeScope&)=delete;
};
}
