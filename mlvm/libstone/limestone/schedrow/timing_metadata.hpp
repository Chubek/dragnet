#pragma once
#include "schedrow.hpp"
#include "metacode/metacode.hpp"
#include <charconv>

// Metadata adapters share one lossless bounds format; algorithms remain
// independent of Metacode and textual parser implementation types.
namespace limestone::schedrow::metadata {
inline LatencyRange latency(const metacode::Value& value) {
  auto number=[](const metacode::Value& value){auto text=value.text();uint32_t n=0;auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),n);if(error!=std::errc{}||end!=text.data()+text.size())throw Error{Error::Code::InvalidArgument,"expected unsigned latency bound: "+text};return n;};
  LatencyRange result;
  if(auto array=std::get_if<metacode::Value::Array>(&value.data)){if(array->size()!=2)throw Error{Error::Code::InvalidArgument,"latency range needs two bounds"};result={number((*array)[0]),number((*array)[1])};}
  else if(auto object=std::get_if<metacode::Value::Object>(&value.data)){if(object->size()!=2||!object->contains("min")||!object->contains("max"))throw Error{Error::Code::InvalidArgument,"latency range needs min and max"};result={number(object->at("min")),number(object->at("max"))};}
  else {auto n=number(value);result={n,n};}
  if(result.minimum>result.maximum)throw Error{Error::Code::InvalidArgument,"reversed latency range"};return result;
}
inline metacode::Value value(LatencyRange range) {
  return metacode::Value(metacode::Value::Array{metacode::Value(uint64_t(range.minimum)),metacode::Value(uint64_t(range.maximum))});
}
}
