#pragma once
#include "bin2bin.hpp"

namespace limestone::bin2bin {
class Runtime;
struct TranslatedRegion {
  uint64_t guest_address=0,target_address=0;
  std::vector<uint8_t> guest_bytes,bytes;
  bool compiled()const{return bool(execute_);}
  bool valid()const{return valid_&&!owner_.expired();}
 private:
  friend class Runtime;
  bool valid_=true;
  std::weak_ptr<const int> owner_;
  std::function<Result<int64_t>()> execute_;
};
struct RuntimeOptions {
  size_t hot_threshold=10,max_regions=1024;
  TranslationOptions translation;
};
// The host owns executable memory, calling conventions, and VM state. Installers
// return an owning callable; native code is never inferred from bytecode domains.
using CodeInstaller=std::function<Result<std::function<Result<int64_t>()>>(const TranslatedRegion&)>;
class Runtime {
  struct Entry {std::shared_ptr<TranslatedRegion> region;size_t visits=0;uint64_t last_use=0;};
  Architecture source_,target_;
  RuntimeOptions options_;
  CodeInstaller installer_;
  TranslationCache cache_;
  std::map<std::string,Entry> regions_;
  std::vector<std::weak_ptr<TranslatedRegion>> published_;
  std::shared_ptr<const int> owner_=std::make_shared<const int>(0);
  uint64_t clock_=0,generation_=0;
  bool preparing_=false,destroying_=false;
 public:
  Runtime(Architecture source,Architecture target,RuntimeOptions options={},CodeInstaller installer={});
  ~Runtime();
  Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;
  // Repeated observations increase heat only for identical bytes and addresses.
  // Noncacheable semantic transforms run on every observation; heat is retained,
  // but translated bytes and installed callables are not reused by prepare.
  // Calls are synchronous; synchronize access to a shared runtime.
  // Executable releases run after residency mutations have completed. They may
  // invalidate code or invoke retained regions; operations during destruction
  // return conflict. An active runtime must outlive every synchronous call.
  Result<std::shared_ptr<const TranslatedRegion>> prepare(std::span<const uint8_t>,uint64_t guest_address,uint64_t target_address=0);
  Result<int64_t> invoke(const std::shared_ptr<const TranslatedRegion>&);
  // Invalidates every overlapping live handle, including handles evicted from LRU.
  Result<size_t> invalidate(uint64_t guest_address,uint64_t byte_count);
  Result<int> open_persistent_cache(const std::string& path,size_t map_size=64*1024*1024);
  size_t resident_regions()const{return regions_.size();}
};
}
