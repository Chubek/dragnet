#include "decompiler.hpp"
#include <set>

namespace limestone::bin2bin {
Result<Decompilation> decompile_with_plugins(const Architecture& architecture,std::span<const uint8_t> bytes,
    std::span<const DecompilerPlugin> plugins,uint64_t address,const DecompilationLimits& limits){
  using Output=Result<Decompilation>;
  try {
    if(bytes.size()>limits.bytes||plugins.size()>limits.plugins)return Output::err({Error::Code::ResourceLimit,"decompilation input/plugin limit"});
    // Retain owners before host code can release the caller's plugin handles.
    std::vector<DecompilerPlugin> active(plugins.begin(),plugins.end());std::set<std::string> identities;
    for(auto& plugin:active)if(plugin.identity.empty()||plugin.language.empty()||!plugin.apply||!identities.insert(plugin.identity).second)return Output::err({Error::Code::InvalidArgument,"invalid or duplicate decompiler plugin"});
    auto cfg=analyze(architecture,bytes,address);if(!cfg)return Output::err(cfg.error());if(cfg.value().instructions.size()>limits.instructions)return Output::err({Error::Code::ResourceLimit,"decompilation instruction limit"});
    auto semantics=lift(architecture,bytes,address);if(!semantics)return Output::err(semantics.error());Decompilation result;
    result.facts={architecture.name,architecture.version,architecture.description,architecture.execution_domain,architecture.state_model,address,{bytes.begin(),bytes.end()},std::move(cfg.value()),std::move(semantics.value())};
    size_t remaining=limits.output_bytes;
    for(auto& plugin:active){auto output=plugin.apply(result.facts);if(!output)return Output::err(output.error());auto& interpretation=output.value();if(interpretation.language!=plugin.language||interpretation.attribution.empty()||interpretation.text.find('\0')!=std::string::npos)return Output::err({Error::Code::Conflict,"decompiler output violates language/attribution contract"});for(auto* field:{&interpretation.text,&interpretation.language,&interpretation.attribution}){if(field->size()>remaining)return Output::err({Error::Code::ResourceLimit,"decompiler output limit"});remaining-=field->size();}result.interpretations.push_back(std::move(interpretation));}
    return Output::ok(std::move(result));
  }catch(const Error& error){return Output::err(error);}catch(const std::bad_alloc&){return Output::err({Error::Code::ResourceLimit,"decompilation allocation failed"});}catch(const std::exception& error){return Output::err({Error::Code::Internal,error.what()});}catch(...){return Output::err({Error::Code::Internal,"decompiler plugin exception"});}
}
}
