#include "selection_internal.hpp"
#include "c_api_internal.hpp"
#include "metacode/json.hpp"

namespace {
struct CallbackOwner {
  void* userdata=nullptr;
  limestone_selection_release release=nullptr;
  ~CallbackOwner() noexcept {if(release)try{release(userdata);}catch(...) {}}
};
}
extern "C" limestone_selection_predicate* limestone_selection_predicate_create(const char* name,
    limestone_selection_proof prove,void* userdata,limestone_selection_release release,limestone_error* error) {
  using namespace limestone;using namespace limestone::c_api_internal;
  return boundary(error,[&]()->limestone_selection_predicate* {
    if(!name||!*name||!prove)throw Error{Error::Code::InvalidArgument,"missing selection predicate name or callback"};
    auto owner=std::make_shared<CallbackOwner>();owner->userdata=userdata;
    auto result=std::make_unique<limestone_selection_predicate>();result->name=name;
    result->prove=[owner,prove](const metacode::Value::Object& context,const metacode::Value::Object& parameters)->Result<bool> {
      auto facts=checked(metacode::print_json(metacode::Value(context))),args=checked(metacode::print_json(metacode::Value(parameters)));
      int proved=0;limestone_error error{};auto status=prove(facts.c_str(),args.c_str(),&proved,owner->userdata,&error);
      if(status!=LIMESTONE_OK){if(status<LIMESTONE_INVALID_ARGUMENT||status>LIMESTONE_RESOURCE_LIMIT)return Result<bool>::err({Error::Code::Internal,"selection callback returned an invalid status"});auto end=std::find(std::begin(error.message),std::end(error.message),'\0');return Result<bool>::err({static_cast<Error::Code>(int(status)-1),end==std::begin(error.message)?"selection callback failed":std::string(std::begin(error.message),end)});}
      if(proved!=0&&proved!=1)return Result<bool>::err({Error::Code::InvalidArgument,"selection callback returned a non-Boolean proof"});
      return Result<bool>::ok(bool(proved));
    };
    owner->release=release;return result.release();
  });
}
extern "C" void limestone_selection_predicate_destroy(limestone_selection_predicate* predicate){delete predicate;}
