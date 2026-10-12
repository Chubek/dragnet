#include "harness.hpp"
#include "editor.hpp"
#include <algorithm>
#include <iostream>
#include <string>

// The REPL frontend is exercised without a terminal: scanner, completion and the
// preamble-plus-submission compile path. Interactive replxx behaviour is covered by
// docs/repl.md rather than by a pty-based test.
namespace {
std::vector<std::string> completion(const std::string& input) {
    int length = static_cast<int>(input.size());
    std::vector<std::string> result;
    for (const auto& candidate : dragscript::repl_detail::complete(input, length))
        result.push_back(candidate.text());
    return result;
}
bool has(const std::vector<std::string>& items, const std::string& item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}
bool colored(replxx::Replxx::colors_t& colors, replxx::Replxx::Color color, const std::string& word) {
    colors.assign(word.size(), replxx::Replxx::Color::DEFAULT);
    dragscript::repl_detail::highlight(word, colors);
    return std::all_of(colors.begin(), colors.end(), [&](auto c) { return c == color; });
}
} // namespace

void repl_tests() {
    // Multiline detection: only genuinely open constructs continue the prompt.
    CHECK(dragscript::repl_detail::needs_more_input("function f() {"));
    CHECK(dragscript::repl_detail::needs_more_input("let a = [1, 2,"));
    CHECK(dragscript::repl_detail::needs_more_input("let s = 'unterminated"));
    CHECK(dragscript::repl_detail::needs_more_input("/* unterminated"));
    CHECK(dragscript::repl_detail::needs_more_input("let a = ("));
    CHECK(!dragscript::repl_detail::needs_more_input("function f() { return 1; }"));
    CHECK(!dragscript::repl_detail::needs_more_input("1 + 1"));
    CHECK(!dragscript::repl_detail::needs_more_input("let a = []"));
    CHECK(!dragscript::repl_detail::needs_more_input("let s = 'done'"));
    // Escaped quotes do not terminate the string, but a closed one does.
    CHECK(dragscript::repl_detail::needs_more_input("let s = 'a\\'"));
    CHECK(!dragscript::repl_detail::needs_more_input("let s = 'a\\'b'"));
    // Template literals may span lines; brackets inside strings do not count.
    CHECK(!dragscript::repl_detail::needs_more_input("let s = '('"));
    CHECK(!dragscript::repl_detail::needs_more_input("let a = {b: 1}"));
    // Mismatched closers are malformed input, not "needs more".
    CHECK(!dragscript::repl_detail::needs_more_input("let a = (]"));
    CHECK(!dragscript::repl_detail::needs_more_input("}"));

    // Completion is prefix anchored and reports the replaced length.
    const auto keywords = completion("fun");
    CHECK(has(keywords, "function"));
    CHECK(!has(keywords, "for"));
    int length = 0;
    dragscript::repl_detail::complete("let x = wh", length);
    CHECK(length == 2);
    CHECK(has(completion(""), "let"));
    CHECK(has(completion("."), ".help"));
    CHECK(has(completion("."), ".quit"));
    CHECK(completion("zzz").empty());
    // Completion is sorted for stable paging.
    CHECK(std::is_sorted(keywords.begin(), keywords.end()));

    // Highlighting covers keywords, strings and comments.
    replxx::Replxx::colors_t colors;
    CHECK(colored(colors, replxx::Replxx::Color::BLUE, "let"));
    CHECK(colored(colors, replxx::Replxx::Color::GREEN, "'ab'"));
    CHECK(colored(colors, replxx::Replxx::Color::GRAY, "// x"));
    CHECK(colored(colors, replxx::Replxx::Color::GRAY, "/*x*/"));
    CHECK(colored(colors, replxx::Replxx::Color::YELLOW, "123"));
    colors.assign(4, replxx::Replxx::Color::DEFAULT);
    dragscript::repl_detail::highlight("abcd", colors);
    CHECK(std::all_of(colors.begin(), colors.end(), [](auto c) { return c == replxx::Replxx::Color::DEFAULT; }));

    // The REPL preamble makes `print` available and remains shadowable by user code.
    const std::string preamble = "function print(value) { return value; }\n";
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        EngineOptions options;
        options.backend = backend;
        Harness harness(options);
        CHECK(harness.run(preamble + "print(6 * 7)").value.number() == 42);
        CHECK(harness.run(preamble + "function twice(x) { return x + x; }\nprint(twice(21))").value.number() == 42);
        CHECK(harness.run(preamble + "print('hello')").value.tag() == Value::Tag::string);
        CHECK(harness.run(preamble + "let a = 0;\nfor (const i of [1,2,3]) { a = a + i; }\nprint(a)").value.number() == 6);
    }

    // Session composition: the preamble and context must not gain a phantom separator,
    // which used to shift every reported line by one.
    const std::string context = "let a = 1\nlet b = 2\n";
    const std::string source = "function g() {\n2 ** 0;\n}\n";
    const auto session = dragscript::repl_detail::compose_session(preamble, context, source);
    CHECK(session.program == preamble + context + source);
    CHECK(session.first_line == 4); // Preamble line, then two context lines.
    // The failing `2 ** 0;` is submission line 2, which is program line 5.
    CHECK(dragscript::repl_detail::submission_line(5, session.first_line) == 2);
    // A first submission starts at program line 2, directly after the preamble.
    const auto first = dragscript::repl_detail::compose_session(preamble, "", "1 + 1\n");
    CHECK(first.first_line == 2);
    CHECK(dragscript::repl_detail::submission_line(2, first.first_line) == 1);
    // Spans inside the preamble or earlier context are not silently rebased.
    CHECK(dragscript::repl_detail::submission_line(1, first.first_line) == 1);
    // Context stays newline-terminated so the next submission is never concatenated.
    CHECK(dragscript::repl_detail::extend_context("", "1 + 1") == "1 + 1\n");
    CHECK(dragscript::repl_detail::extend_context("1 + 1\n", "2 + 2\n") == "1 + 1\n2 + 2\n");
    CHECK(dragscript::repl_detail::extend_context("1 + 1\n", "2 + 2") == "1 + 1\n2 + 2\n");

    // Declarations must survive across submissions: the REPL recompiles the session,
    // so an earlier `function` and `let` stay in scope for a later submission.
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        EngineOptions options;
        options.backend = backend;
        Harness harness(options);
        std::string context;
        const auto submit = [&](const std::string& text) {
            const auto composed = dragscript::repl_detail::compose_session(preamble, context, text);
            const auto outcome = harness.run(composed.program);
            if (outcome.ok) context = dragscript::repl_detail::extend_context(context, text);
            return outcome;
        };
        CHECK(submit("function twice(x) { return x + x; }\n").ok);
        CHECK(submit("let base = 20;\n").ok);
        const auto third = submit("print(twice(base))\n");
        CHECK(third.ok);
        CHECK(third.value.number() == 40);
        // A rejected submission is not folded into the session context.
        const auto rejected = submit("2 ** 0\n");
        CHECK(!rejected.ok);
        CHECK(rejected.code == "DS_UNSUPPORTED");
        const auto after = submit("print(base)\n");
        CHECK(after.ok);
        CHECK(after.value.number() == 20);
    }
}
int main() {
    try {
        repl_tests();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
