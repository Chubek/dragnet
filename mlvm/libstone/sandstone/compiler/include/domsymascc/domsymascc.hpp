// DomSymAsCC — the DomSymAs compiler (public C++ API).
//
// DomSymAsCC compiles `.dsymspec` files (a Lua DSL run under the embedded
// DomMune interpreter) into SymAS databanks (`.symas`) via the `domsymas`
// Builder. The DSL accumulates a structured description of an assembler
// target, serializes it to JSON on stdout, and this library maps that
// document onto `domsymas::Builder`.
//
// Layering:
//   .dsymspec (Lua DSL)  ->  JSON document  ->  domsymas::Builder  ->  .symas
//   (DomMune embedded)      (this header)      (domsymas lib)
//
// The JSON schema is documented in `stdspec/README.md` and implemented in
// `src/spec_document.cpp`.
#pragma once

#include <cstdint>
#include <string>

#include "sandstone.hpp"     // domsymas::Builder, domsymas::Databank
#include "json.hpp"          // domsymas::cc::json

namespace domsymas::cc {

// Current DSL/schema version this compiler understands.
inline constexpr uint32_t kSchemaVersion = 1;

// Result of a compile step.
struct Result {
    bool ok = false;
    std::string error;                 // human-readable diagnostic
    std::string spec_json;             // the JSON document (for --emit-spec)
};

// ---- Pure document -> builder mapping (no DomMune required) ----------------
//
// `document` is the JSON produced by the DSL (the value of `symas.emit()`).
// On success the supplied builder has been populated with all records and
// cross-links; the caller still owns the final `build()`/`save_file()` step.
Result compile_document(const std::string &document_json, domsymas::Builder &builder);

// Convenience: compile a document straight into an in-memory databank.
Result compile_document_to_bank(const std::string &document_json, domsymas::Databank &out);

} // namespace domsymas::cc
