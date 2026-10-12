// DomSymAs sections + linking metadata accessors.
#include "sandstone.hpp"

namespace domsymas {

const SectionRec *Databank::find_section(const std::string &name) const {
    for (auto &s : data_.sections)
        if (s.name == name) return &s;
    return nullptr;
}

} // namespace domsymas
