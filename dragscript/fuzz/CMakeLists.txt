# Fuzzing CMake configuration for Dragscript
# Usage: cmake -DCMAKE_C_COMPILER=afl-clang-fast -DCMAKE_CXX_COMPILER=afl-clang-fast++ -DFUZZING=ON ..

if(DRAGSCRIPT_FUZZING)
    message(STATUS "Configuring for AFL++ fuzzing")
    
    # Require AFL++ compilers
    if(NOT CMAKE_C_COMPILER MATCHES "afl-")
        message(FATAL_ERROR "DRAGSCRIPT_FUZZING=ON requires AFL++ compiler (set CC=afl-clang-fast CXX=afl-clang-fast++)")
    endif()
    
    # Fuzzing-specific flags
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
    
    # Disable optimizations that interfere with coverage
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-inline -fno-inline-functions -fno-optimize-sibling-calls")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-inline -fno-inline-functions -fno-optimize-sibling-calls")
    
    # Enable AFL++ instrumentation (automatic with afl-clang-fast++)
    
    add_executable(fuzz_compile 
        fuzz_compile.cc
    )
    target_link_libraries(fuzz_compile PRIVATE Dragscript::Core)
    target_include_directories(fuzz_compile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    
    message(STATUS "Fuzzing targets: fuzz_compile")
    message(STATUS "Note: persistent mode fuzzers require libafl.a from AFL++ source build")
endif()