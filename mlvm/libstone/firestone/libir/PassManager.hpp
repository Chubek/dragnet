#pragma once
#include "SimpleSSAGraph.hpp"

namespace firestone::ir {

struct PassReport {
    std::string name;
    bool changed = false, committed = false;
    Diagnostics diagnostics;
};

class PassManager {
public:
    using Pass = std::function<bool(SimpleSSAGraph&)>;
    void add(std::string name, Pass pass) {
        if (name.empty() || !pass) throw std::invalid_argument("pass requires a name and callback");
        passes_.emplace_back(std::move(name), std::move(pass));
    }
    // Invalid transformations are rolled back and stop the pipeline. Exceptions
    // also roll back the active pass before propagating to the caller.
    std::vector<PassReport> run(SimpleSSAGraph& program) const {
        auto errors = program.verify();
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        std::vector<PassReport> reports;
        for (const auto& pass : passes_) {
            auto snapshot = program;
            PassReport report; report.name = pass.first;
            try { report.changed = pass.second(program); report.diagnostics = program.verify(); }
            catch (...) { program = std::move(snapshot); throw; }
            report.committed = report.diagnostics.empty();
            if (!report.committed) program = std::move(snapshot);
            reports.push_back(std::move(report));
            if (!reports.back().committed) break;
        }
        return reports;
    }
private:
    std::vector<std::pair<std::string, Pass>> passes_;
};

} // namespace firestone::ir
