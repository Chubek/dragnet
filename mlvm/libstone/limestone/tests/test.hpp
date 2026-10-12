#pragma once
#include "limestone/foundation.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <map>
#include <source_location>

#define CHECK(...) do { if(!(__VA_ARGS__))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #__VA_ARGS__); } while(false)
template<class T> T take(limestone::Result<T> result) {
  if(!result)throw std::runtime_error(result.error().message);
  return std::move(result.value());
}
template<class T> void fails(const limestone::Result<T>& result,limestone::Error::Code code,const std::source_location& source=std::source_location::current()) {
  if(result)throw std::runtime_error(std::string(source.file_name())+":"+std::to_string(source.line())+": expected failure");
  if(result.error().code!=code)throw std::runtime_error("expected error "+std::to_string(static_cast<int>(code))+", got "+std::to_string(static_cast<int>(result.error().code))+": "+result.error().message);
  CHECK(!result.error().message.empty());
}
template<class F> int test_main(F test) {
  try {test();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
