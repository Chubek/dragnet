#include "runtime.hpp"
#include <limits>

namespace limestone::bin2bin {
Runtime::Runtime(Architecture source,Architecture target,RuntimeOptions options,CodeInstaller installer)
  :source_(std::move(source)),target_(std::move(target)),options_(std::move(options)),installer_(std::move(installer)) {}
Runtime::~Runtime() {
  destroying_=true;owner_.reset();published_.clear();
  // Destroy host callables while the runtime is still inspectable and empty.
  // Their release hooks must not run inside map erasure or member destruction.
  decltype(regions_) retired;retired.swap(regions_);
  CodeInstaller installer;installer.swap(installer_);
   std::optional<SemanticTransform> transform;transform.swap(options_.translation.semantic_transform);
   std::optional<RegionTransform> region_transform;region_transform.swap(options_.translation.region_transform);
}
Result<std::shared_ptr<const TranslatedRegion>> Runtime::prepare(std::span<const uint8_t> bytes,uint64_t guest_address,uint64_t target_address) {
  using Output=Result<std::shared_ptr<const TranslatedRegion>>;
  if(destroying_)return Output::err({Error::Code::Conflict,"runtime is being destroyed"});
  if(preparing_)return Output::err({Error::Code::Conflict,"recursive runtime preparation"});
  if(bytes.empty()||bytes.size()-1>UINT64_MAX-guest_address||!options_.hot_threshold||!options_.max_regions)return Output::err({Error::Code::InvalidArgument,"invalid runtime region, capacity, or hot threshold"});
  if(clock_==UINT64_MAX)return Output::err({Error::Code::ResourceLimit,"runtime observation clock exhausted"});
  preparing_=true;struct Reset {bool& flag;~Reset(){flag=false;}} reset{preparing_};
  try {
  std::vector<std::shared_ptr<TranslatedRegion>> retired;
  std::erase_if(published_,[](auto& pointer){return pointer.expired();});
  auto generation=generation_;
    bool cacheable=(!options_.translation.semantic_transform||options_.translation.semantic_transform->cacheable)&&
      (!options_.translation.region_transform||options_.translation.region_transform->cacheable)&&
      (!source_.codec||source_.codec->cacheable)&&(!target_.codec||target_.codec->cacheable);
  std::string key=std::to_string(guest_address)+":"+std::to_string(target_address)+":";
  key.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  auto found=regions_.find(key);
  if(found==regions_.end()||!cacheable) {
    auto options=options_.translation;options.source_address=guest_address;options.target_address=target_address;
    auto translated=translate(source_,target_,bytes,cacheable?&cache_:nullptr,options);if(!translated)return Output::err(translated.error());
    // Host semantic analyses may invalidate code while translation is in flight.
    if(generation!=generation_)return Output::err({Error::Code::Interrupted,"region invalidated during translation"});
    auto region=std::make_shared<TranslatedRegion>();region->guest_address=guest_address;region->target_address=target_address;region->guest_bytes.assign(bytes.begin(),bytes.end());region->bytes=std::move(translated.value());region->owner_=owner_;
    published_.push_back(region);
    if(found==regions_.end())found=regions_.emplace(key,Entry{region,0,0}).first;
    else {retired.push_back(std::move(found->second.region));found->second.region=std::move(region);}
  }
  auto region=found->second.region;
  if(found->second.visits!=std::numeric_limits<size_t>::max())++found->second.visits;
  found->second.last_use=++clock_;
  bool hot=found->second.visits>=options_.hot_threshold;
  while(regions_.size()>options_.max_regions) {
    auto oldest=std::min_element(regions_.begin(),regions_.end(),[](auto& a,auto& b){return a.second.last_use<b.second.last_use;});
    auto entry=regions_.extract(oldest);retired.push_back(std::move(entry.mapped().region));
  }
  if(cache_.entries.size()>options_.max_regions)cache_.entries.clear();
  // Releasing evicted/replaced code can call back into invalidation. No iterator
  // crosses that boundary, and in-flight publication observes its generation.
  retired.clear();
  if(generation!=generation_||!region->valid())return Output::err({Error::Code::Interrupted,"region invalidated during executable release"});
  if(installer_&&hot&&!region->compiled()) {
      auto installed=installer_(*region);if(!installed)return Output::err(installed.error());
      if(!installed.value())return Output::err({Error::Code::Conflict,"installer returned an empty callable"});
      if(generation!=generation_||!region->valid())return Output::err({Error::Code::Interrupted,"region invalidated during installation"});
      // Replace the immutable published view rather than mutate a cold handle.
      auto compiled=std::make_shared<TranslatedRegion>(*region);compiled->execute_=std::move(installed.value());
      auto current=regions_.find(key);if(current==regions_.end())return Output::err({Error::Code::Interrupted,"region removed during installation"});
      published_.push_back(compiled);current->second.region=compiled;region=std::move(compiled);
  }
  std::erase_if(published_,[](auto& pointer){return pointer.expired();});
  // The translated-byte memoization is subordinate to the bounded region cache.
  return Output::ok(std::move(region));
  }catch(const Error& e){return Output::err(e);}
  catch(const std::bad_alloc&){return Output::err({Error::Code::ResourceLimit,"runtime preparation allocation failed"});}
  catch(const std::exception& e){return Output::err({Error::Code::Internal,std::string("runtime preparation: ")+e.what()});}
  catch(...){return Output::err({Error::Code::Internal,"runtime preparation exception"});}
}
Result<int64_t> Runtime::invoke(const std::shared_ptr<const TranslatedRegion>& region) {
  if(destroying_)return Result<int64_t>::err({Error::Code::Conflict,"runtime is being destroyed"});
  // An execution adapter may invalidate its region and release the caller's
  // handle. Keep its executable owner alive until the active call returns.
  auto active=region;
  if(!active||active->owner_.lock()!=owner_)return Result<int64_t>::err({Error::Code::InvalidArgument,"region belongs to another runtime"});
  if(!active->valid())return Result<int64_t>::err({Error::Code::Conflict,"translated region was invalidated"});
  if(!active->compiled())return Result<int64_t>::err({Error::Code::Unsupported,"region has no installed executable adapter"});
  try{return active->execute_();}catch(const Error& e){return Result<int64_t>::err(e);}catch(const std::bad_alloc&){return Result<int64_t>::err({Error::Code::ResourceLimit,"runtime execution allocation failed"});}catch(const std::exception& e){return Result<int64_t>::err({Error::Code::Internal,std::string("runtime execution: ")+e.what()});}catch(...){return Result<int64_t>::err({Error::Code::Internal,"runtime execution exception"});}
}
Result<size_t> Runtime::invalidate(uint64_t guest_address,uint64_t count) {
  if(destroying_)return Result<size_t>::err({Error::Code::Conflict,"runtime is being destroyed"});
  if(!count)return Result<size_t>::ok(0);
  if(count-1>UINT64_MAX-guest_address)return Result<size_t>::err({Error::Code::InvalidArgument,"runtime invalidation range overflow"});
  if(generation_==UINT64_MAX)return Result<size_t>::err({Error::Code::ResourceLimit,"runtime invalidation generation exhausted"});
  ++generation_;auto last=guest_address+count-1;size_t removed=0;
  for(auto& weak:published_)if(auto region=weak.lock())if(region->valid_&&region->guest_address<=last&&guest_address<=region->guest_address+region->guest_bytes.size()-1){region->valid_=false;++removed;}
  decltype(regions_) retired;
  for(auto it=regions_.begin();it!=regions_.end();)if(!it->second.region->valid()){auto removed=it++;retired.insert(regions_.extract(removed));}else ++it;
  std::erase_if(published_,[](auto& pointer){return pointer.expired();});cache_.entries.clear();
  return Result<size_t>::ok(removed);
}
Result<int> Runtime::open_persistent_cache(const std::string& path,size_t map_size) {
  if(destroying_||preparing_)return Result<int>::err({Error::Code::Conflict,"cache configuration during runtime preparation or destruction"});return open_cache(cache_,path,map_size);
}
}
