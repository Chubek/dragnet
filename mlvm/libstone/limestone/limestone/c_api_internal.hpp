#pragma once
#include "limestone.h"
#include "foundation.hpp"
#include <cstring>
#include <exception>

namespace limestone::c_api_internal {
inline void diagnostic(limestone_error* out,limestone_status code,const char* message) noexcept {
  if(!out)return;out->code=code;
  std::strncpy(out->message,message,sizeof(out->message)-1);out->message[sizeof(out->message)-1]=0;
}
template<class F> auto boundary(limestone_error* error,F operation)->decltype(operation()) {
  limestone_error local{};if(!error)error=&local;diagnostic(error,LIMESTONE_OK,"");
  try{return operation();}
  catch(const Error& e){diagnostic(error,static_cast<limestone_status>(static_cast<int>(e.code)+1),e.message.c_str());}
  catch(const std::bad_alloc&){diagnostic(error,LIMESTONE_RESOURCE_LIMIT,"allocation failed");}
  catch(const std::exception& e){diagnostic(error,LIMESTONE_INTERNAL,e.what());}
  catch(...){diagnostic(error,LIMESTONE_INTERNAL,"compiler exception");}
  using R=decltype(operation());if constexpr(std::is_pointer_v<R>)return nullptr;else return error->code;
}
template<class T> T checked(Result<T> result){if(!result)throw result.error();return std::move(result.value());}
}
