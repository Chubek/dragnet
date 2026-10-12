#include "Test.hpp"
using namespace firestone::rewrite;

int main() { return test_main([] {
    auto term = parse("(add (mul x 1) 0)"); CHECK(parse(to_string(term)) == term);
    auto quoted = Term("a \"b\\c", {Term("line\nend")}); CHECK(parse(to_string(quoted)) == quoted);
    for (const auto& invalid : {"", "()", "(add x", "x y", "\"unterminated", "(x))", "(x () y)"}) throws<std::invalid_argument>([&] { parse(invalid); });
    CHECK(match(parse("(add ?x ?x)"), parse("(add z z)")));
    CHECK(!match(parse("(add ?x ?x)"), parse("(add z y)")));
    throws<std::invalid_argument>([] { Rule("bad", parse("?x"), parse("?y")); });
    throws<std::invalid_argument>([] { match(parse("(?x y)"), parse("(a y)")); });
    Rewriter rewriter; rewriter.add_rule({"mul-one", parse("(mul ?x 1)"), parse("?x")}); rewriter.add_rule({"add-zero", parse("(add ?x 0)"), parse("?x")});
    auto result = rewriter.run(term); CHECK(result.term == Term("x")); CHECK(result.stop == RewriteStop::NormalForm); CHECK(result.applications == 2);
    auto limited = rewriter.run(term, 100, 1); CHECK(limited.stop == RewriteStop::ApplicationLimit); CHECK(limited.applications == 1);
    Rewriter guarded; guarded.add_rule({"guard", parse("(f ?x)"), parse("?x"), [](const Substitution& s) { return s.at("?x").op == "safe"; }});
    CHECK(guarded.run(parse("(f safe)")).term == Term("safe")); CHECK(guarded.run(parse("(f unsafe)")).applications == 0);
    Rewriter oscillating; oscillating.add_rule({"a-b", Term("a"), Term("b")}); oscillating.add_rule({"b-a", Term("b"), Term("a")});
    CHECK(oscillating.run(Term("a"), 5).stop == RewriteStop::IterationLimit);
}); }
