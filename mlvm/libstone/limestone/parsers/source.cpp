#include "source.hpp"
#include <array>
#include <filesystem>
#include <fstream>

namespace limestone::syntax {
IncludeResolver filesystem_resolver(size_t byte_limit) {
  return [byte_limit](std::string_view including,std::string_view requested)->Result<ResolvedSource> {
    if(requested.empty()||requested.find('\0')!=requested.npos)return Result<ResolvedSource>::err({Error::Code::InvalidArgument,"invalid include path"});
    try {
      std::filesystem::path path{std::string(requested)};
      if(path.is_relative()&&!including.empty())path=std::filesystem::path(std::string(including)).parent_path()/path;
      std::error_code error;auto canonical=std::filesystem::canonical(path,error);
      if(error)return Result<ResolvedSource>::err({Error::Code::NotFound,"cannot resolve "+path.string()+": "+error.message()});
      std::ifstream input(canonical,std::ios::binary);if(!input)return Result<ResolvedSource>::err({Error::Code::NotFound,"cannot open "+canonical.string()});
      ResolvedSource source{canonical.string(),{}};std::array<char,8192> buffer;
      while(input) {
        input.read(buffer.data(),buffer.size());auto count=static_cast<size_t>(input.gcount());
        if(count>byte_limit-source.text.size())return Result<ResolvedSource>::err({Error::Code::ResourceLimit,"source byte limit exceeded: "+source.name});
        source.text.append(buffer.data(),count);
      }
      if(input.bad())return Result<ResolvedSource>::err({Error::Code::Internal,"cannot read "+source.name});
      return Result<ResolvedSource>::ok(std::move(source));
    }catch(const std::exception& e){return Result<ResolvedSource>::err({Error::Code::Internal,std::string("source resolver: ")+e.what()});}
  };
}
}
