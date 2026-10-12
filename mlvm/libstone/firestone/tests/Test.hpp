#pragma once
#include "Firestone.hpp"
#include <iostream>
#include <stdexcept>

#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)

template<class Exception = std::exception, class Callable>
void throws(Callable&& callable) {
    bool caught = false;
    try { callable(); } catch (const Exception&) { caught = true; }
    CHECK(caught);
}

inline bool has_code(const firestone::ir::Diagnostics& errors, const std::string& code) {
    for (const auto& error : errors) if (error.code == code) return true;
    return false;
}

template<class Callable>
int test_main(Callable&& callable) {
    try { callable(); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
