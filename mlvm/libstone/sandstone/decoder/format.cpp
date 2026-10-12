// DomSymAs format constants: versions, section names, error strings.
#include "sandstone.hpp"

namespace domsymas {

const char *error_string(DomSymAs_Error e) {
    switch (e) {
    case DOMSYMAS_OK: return "ok";
    case DOMSYMAS_ERR_IO: return "I/O error";
    case DOMSYMAS_ERR_NOMEM: return "out of memory";
    case DOMSYMAS_ERR_BAD_MAGIC: return "bad SymAS magic (expected 0xCEEB09)";
    case DOMSYMAS_ERR_BAD_VERSION: return "unsupported databank version";
    case DOMSYMAS_ERR_BAD_COMPRESSION: return "unknown or unavailable compression method";
    case DOMSYMAS_ERR_BAD_CHECKSUM: return "checksum mismatch";
    case DOMSYMAS_ERR_BAD_BOUNDS: return "section out of bounds";
    case DOMSYMAS_ERR_BAD_STRING_REF: return "string reference outside string table";
    case DOMSYMAS_ERR_BAD_RECORD_REF: return "dangling record reference";
    case DOMSYMAS_ERR_BAD_FORM_REF: return "dangling instruction-form reference";
    case DOMSYMAS_ERR_BAD_ENCODER_REF: return "dangling encoding-function reference";
    case DOMSYMAS_ERR_BAD_FEATURE_REF: return "dangling feature reference";
    case DOMSYMAS_ERR_BAD_RELOC: return "invalid relocation field";
    case DOMSYMAS_ERR_BAD_ALIGN: return "invalid alignment";
    case DOMSYMAS_ERR_BAD_FLAGS: return "unknown mandatory flags or fields";
    case DOMSYMAS_ERR_TRUNCATED: return "truncated input";
    case DOMSYMAS_ERR_OVERFLOW: return "integer overflow in offset calculation";
    case DOMSYMAS_ERR_ENCODE: return "encoding-function error";
    case DOMSYMAS_ERR_NOT_FOUND: return "record not found";
    default: return "unknown error";
    }
}

const char *section_name(Section s) {
    switch (s) {
    case Section::Strings: return "STRINGS";
    case Section::Target: return "TARGET";
    case Section::Registers: return "REGISTERS";
    case Section::Features: return "FEATURES";
    case Section::Mnemonics: return "MNEMONICS";
    case Section::Opcodes: return "OPCODES";
    case Section::Operands: return "OPERANDS";
    case Section::Encoders: return "ENCODERS";
    case Section::Syntax: return "SYNTAX";
    case Section::Sections: return "SECTIONS";
    case Section::Linking: return "LINKING";
    case Section::Relocations: return "RELOCATIONS";
    case Section::Index: return "INDEX";
    case Section::Debug: return "DEBUG";
    case Section::Isel: return "ISEL";
    case Section::Sched: return "SCHED";
    case Section::Units: return "UNITS";
    case Section::Models: return "MODELS";
    case Section::Tuning: return "TUNING";
    case Section::Fusion: return "FUSION";
    }
    return "UNKNOWN";
}

bool section_name_to_id(const std::string &name, Section &out) {
    for (int i = 1; i <= (int)kSectionMax; i++) {
        Section s = (Section)i;
        if (name == section_name(s)) { out = s; return true; }
    }
    return false;
}

} // namespace domsymas
