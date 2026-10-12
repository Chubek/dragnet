// DomSymAs relocations: lookup, overflow diagnosis, expression model.
#include "sandstone.hpp"

namespace domsymas {

const RelocRec *Databank::find_reloc(const std::string &name) const {
    for (auto &r : data_.relocs)
        if (r.name == name) return &r;
    return nullptr;
}

// Width is in bits; signedness per record. overflow==0 means "diagnose",
// overflow==1 means "truncate silently".
bool Databank::reloc_fits(const RelocRec &r, int64_t value, std::string *diag) const {
    if (r.width == 0 || r.width > 64) {
        if (diag) *diag = "invalid relocation field width";
        return false;
    }
    if (r.width >= 64) return true;
    if (r.is_signed) {
        int64_t lo = -(1LL << (r.width - 1));
        int64_t hi = (1LL << (r.width - 1)) - 1;
        if (value < lo || value > hi) {
            if (diag)
                *diag = "relocation '" + r.name + "' overflows signed " +
                        std::to_string(r.width) + "-bit field";
            return false;
        }
    } else {
        if (value < 0 || (uint64_t)value >= (1ULL << r.width)) {
            if (diag)
                *diag = "relocation '" + r.name + "' overflows unsigned " +
                        std::to_string(r.width) + "-bit field";
            return false;
        }
    }
    return true;
}

} // namespace domsymas
