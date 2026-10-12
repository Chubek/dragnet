#pragma once
#include <mlvm/ir.hpp>

namespace mlvm {
struct ConversionEdge {
    std::string source, target;
    std::function<bool(Operation&, Module&, DiagnosticEngine&)> rewrite;
};
class ConversionGraph {
public:
    bool add(ConversionEdge edge);
    [[nodiscard]] std::optional<std::vector<std::size_t>> path(std::string_view source, std::string_view target) const;
    // Full conversion, transactional: every operation must belong to a legal dialect on success.
    bool convert(Module& module, const std::set<std::string>& legal, DiagnosticEngine& diagnostics) const;
private:
    std::vector<ConversionEdge> edges_;
};
} // namespace mlvm
