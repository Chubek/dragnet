#pragma once
#include "vmweave.hpp"
#include <sstream>
#include <iomanip>
namespace limestone::vmweave {
inline Result<Module> checked_module(const Module& m) {
  auto s=m.configuration; s.instructions.clear();
  for(const auto& w:m.words) s.instructions.push_back({w.name,w.c_body,w.effect,w.flow,w.opcode,w.operands,w.source});
  return lower(std::move(s));
}
inline std::string c_string(std::string_view s) {
  std::ostringstream out; out<<'"';
  for(unsigned char c:s) {
    if(c=='"' || c=='\\') out<<'\\'<<static_cast<char>(c);
    else if(c<32 || c>=127) out<<'\\'<<std::oct<<std::setw(3)<<std::setfill('0')<<unsigned(c)<<std::dec;
    else out<<static_cast<char>(c);
  }
  out<<'"'; return out.str();
}
}
