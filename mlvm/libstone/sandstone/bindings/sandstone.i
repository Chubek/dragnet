/* Sandstone SWIG 4.0 interface: SymAS databank + assembler C APIs.
 *
 * Build with bindings/generate-bindings.sh, e.g.:
 *   ./bindings/generate-bindings.sh --language python --out build/bindings
 *
 * The wrapped APIs use explicit ownership (see sandstone.h / assembler.h):
 *  - handles (DomSymAs_Bank *, sandstone_asm_t *) are owned by the caller;
 *  - `const` record pointers are borrowed from the bank;
 *  - encode/disassemble outputs (DomSymAs_Bytes, DomSymAs_RelocOut,
 *    DomSymAs_Candidate) are caller-owned: release with domsymas_free*().
 * Array out-parameters (find_mnemonic, resolve, ...) are plain C arrays;
 * the uint32 helper below provides allocation and indexed access.
 */
%module(docstring="Sandstone SymAS databank and assembler bindings.") sandstone

%{
#include "sandstone.h"
#include "assembler.h"
%}

/* Fixed-width integer spellings for target languages. */
%include "stdint.i"

/* Indexed uint32 arrays for the `uint32_t *out, cap, *out_n` idiom. */
%include "carrays.i"
%array_functions(uint32_t, sandstone_u32array);

/* Python `bytes` for `(const uint8_t *data, size_t len)` buffer pairs
 * (domsymas_probe_magic, domsymas_crc32, domsymas_open_memory,
 * sandstone_asm_new_from_memory): pass one bytes object, the length is
 * taken from the object itself. */
%typemap(in) (const uint8_t *data, size_t len) {
    if (!PyBytes_Check($input)) {
        SWIG_exception_fail(SWIG_TypeError, "bytes expected");
    }
    /* The callee treats the buffer as read-only (const); the cast only
     * satisfies SWIG's non-const temporary. */
    $1 = (uint8_t *)PyBytes_AsString($input);
    $2 = (size_t)PyBytes_Size($input);
}

%include "sandstone.h"
%include "assembler.h"
