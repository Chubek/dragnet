#include "harness.hpp"

void frontend_tests();
void runtime_tests();
void backend_tests();
void embedding_tests();
void limits_tests();
void lowering_tests();
void fuzz_smoke_tests();

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::string_view group = argv[1];
        if (group == "frontend") frontend_tests();
        else if (group == "runtime") runtime_tests();
        else if (group == "backend") backend_tests();
        else if (group == "embedding") embedding_tests();
        else if (group == "limits") limits_tests();
        else if (group == "lowering") lowering_tests();
        else if (group == "fuzz-smoke") fuzz_smoke_tests();
        else throw std::runtime_error("Unknown test group " + std::string(group));
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}