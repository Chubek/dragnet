#include <dragscript/repl.hpp>
#include "editor.hpp"
#include <algorithm>
#include <iostream>

namespace dragscript {
namespace {
constexpr const char* kGreen = "\x1b[32m";
constexpr const char* kRed = "\x1b[31m";
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kPrimaryPrompt = "\x1b[1;32mdragscript\x1b[0m> ";
constexpr const char* kContinuationPrompt = "\x1b[1;33m...\x1b[0m> ";
// A submission declares `print` because completion offers it; the engine has no stdio host.
constexpr const char* kPreamble = "function print(value) { return value; }\n";
constexpr std::size_t kContextLimit = 64 * 1024;

std::string join(const std::string& head, const std::string& tail) {
    return head.empty() || tail.empty() ? head + tail : head + "\n" + tail;
}
} // namespace

Repl::Repl(ReplConfig config) : config_(std::move(config)) { select_backend(config_.backend); }

// The engine backend is fixed at construction: EngineOptions is not exposed for mutation,
// so `.backend` rebuilds the engine instead of editing shared state.
void Repl::select_backend(Backend backend) {
    EngineOptions options;
    options.backend = backend;
    engine_ = std::make_unique<Engine>(options);
    config_.backend = backend;
}

void Repl::run() {
    editor_.install_window_change_handler();
    if (!config_.history_file.empty()) editor_.history_load(config_.history_file.string());
    editor_.set_max_history_size(1000);
    editor_.set_word_break_characters(" \t\n.,;:()[]{}<>+-*/%=&|^!~`?\"'");
    editor_.set_completion_callback(
        [](const std::string& input, int& length) { return repl_detail::complete(input, length); });
    editor_.set_highlighter_callback(
        [](const std::string& input, replxx::Replxx::colors_t& colors) { repl_detail::highlight(input, colors); });
    editor_.set_prompt(kPrimaryPrompt);
    std::cout << "Dragscript REPL. Type .help for commands, .quit to exit.\n";
    while (!stopped_) {
        const char* line = editor_.input(pending_.empty() ? kPrimaryPrompt : kContinuationPrompt);
        if (line == nullptr) break; // EOF or Ctrl-D.
        editor_.history_add(line);
        if (!pending_.empty()) pending_ = join(pending_, line);
        else if (config_.multiline && repl_detail::needs_more_input(line)) pending_ = line;
        else { command(line); continue; }
        if (!repl_detail::needs_more_input(pending_)) { execute(pending_); pending_.clear(); }
    }
    if (!config_.history_file.empty()) editor_.history_save(config_.history_file.string());
}

void Repl::command(const std::string& input) {
    if (input.empty()) return;
    if (input.front() != '.') { execute(input); return; }
    const auto space = input.find(' ');
    const std::string name = input.substr(0, space);
    const std::string argument = space == std::string::npos ? "" : input.substr(space + 1);
    if (name == ".help") {
        std::cout << ".help  .quit/.exit  .clear  .history  .reset  .backend dsir|mlvm\n";
    } else if (name == ".quit" || name == ".exit") {
        stopped_ = true;
    } else if (name == ".clear") {
        std::cout << "\x1b[2J\x1b[H";
    } else if (name == ".history") {
        for (auto scan = editor_.history_scan(); scan.next();) std::cout << scan.get().text() << '\n';
    } else if (name == ".reset") {
        select_backend(config_.backend);
        context_.clear();
        std::cout << "Session reset on the " << (config_.backend == Backend::dsir ? "dsir" : "mlvm_ir")
                  << " backend.\n";
    } else if (name == ".backend") {
        if (argument == "dsir") select_backend(Backend::dsir);
        else if (argument == "mlvm" || argument == "mlvm_ir") select_backend(Backend::mlvm_ir);
        else if (!argument.empty()) { std::cout << "Unknown backend: " << argument << '\n'; return; }
        std::cout << "Backend: " << (config_.backend == Backend::dsir ? "dsir" : "mlvm_ir") << '\n';
    } else {
        std::cout << "Unknown command " << name << ". Type .help.\n";
    }
}

void Repl::execute(const std::string& source) {
    if (source.empty()) return;
    if (context_.size() + source.size() > kContextLimit) {
        std::cerr << kRed << "DS_LIMIT" << kReset << " Session context is full; use .reset to start over.\n";
        return;
    }
    // Recompile the whole session so declarations from earlier submissions are in scope.
    const auto session = repl_detail::compose_session(kPreamble, context_, source);
    Source descriptor;
    descriptor.name = "<repl>";
    descriptor.text = session.program;
    descriptor.kind = SourceKind::typescript;
    const auto compiled = engine_->compile(std::move(descriptor));
    if (!compiled.script.valid()) {
        report(session.first_line, compiled.diagnostics);
        return;
    }
    const auto result = engine_->run(compiled.script);
    if (!result.ok()) {
        report(session.first_line, result.diagnostics);
        return;
    }
    // Only a successful submission becomes part of the session context.
    context_ = repl_detail::extend_context(context_, source);
    std::cout << kGreen << result.value.display() << kReset << '\n';
}

void Repl::report(std::uint32_t first_line, const std::vector<Diagnostic>& diagnostics) const {
    for (const auto& diagnostic : diagnostics) {
        const auto line = repl_detail::submission_line(diagnostic.span.begin.line, first_line);
        std::cerr << kRed << diagnostic.code << kReset << " " << diagnostic.message << " at line " << line << '\n';
        for (const auto& frame : diagnostic.trace)
            std::cerr << "  at " << frame.function << " (line " << frame.span.begin.line << ")\n";
    }
}
} // namespace dragscript