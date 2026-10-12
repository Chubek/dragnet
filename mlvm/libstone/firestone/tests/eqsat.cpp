#include "Test.hpp"
using namespace firestone::eqsat;
using firestone::rewrite::parse;
using firestone::rewrite::Term;

int main() { return test_main([] {
    EGraph graph;
    auto x = graph.add(Term("x")), y = graph.add(Term("y"));
    auto fx = graph.add(ENode{"f", {x}}), fy = graph.add(ENode{"f", {y}});
    auto gfx = graph.add(ENode{"g", {fx}}), gfy = graph.add(ENode{"g", {fy}});
    CHECK(!graph.equivalent(gfx, gfy)); graph.merge(x, y); graph.rebuild(); CHECK(graph.equivalent(fx, fy)); CHECK(graph.equivalent(gfx, gfy));
    CHECK(graph.add(parse("(g (f x))")) == graph.find(gfx)); CHECK(graph.node_count() == 4);
    throws<std::out_of_range>([&] { graph.add(ENode{"bad", {999}}); });
    CHECK(search(graph, parse("(g (f ?v))")).matches.size() == 1);

    EGraph expressions; auto root = expressions.add(parse("(add (mul x 1) 0)"));
    Saturator saturation; saturation.add_rule({"mul-one", parse("(mul ?x 1)"), parse("?x")}); saturation.add_rule({"add-zero", parse("(add ?x 0)"), parse("?x")});
    auto result = saturation.run(expressions); CHECK(result.stop == SaturationStop::Saturated);
    CHECK(expressions.extract(root).term == Term("x")); CHECK(expressions.extract(root).cost == 1);
    CHECK(expressions.equivalent(root, expressions.add(Term("x"))));

    EGraph repeated; repeated.add(parse("(add x y)")); auto xx = repeated.add(parse("(add x x)"));
    CHECK(search(repeated, parse("(add ?x ?x)")).matches.size() == 1);
    repeated.merge(repeated.add(Term("x")), repeated.add(Term("y"))); repeated.rebuild();
    CHECK(search(repeated, parse("(add ?x ?x)")).matches.size() == 1); CHECK(repeated.extract(xx).cost == 3);

    EGraph cyclic; auto a = cyclic.add(Term("a")), fa = cyclic.add(ENode{"f", {a}}); cyclic.merge(a, fa); cyclic.rebuild();
    CHECK(cyclic.extract(a).term == Term("a")); CHECK(cyclic.extract(a, [](const ENode&) { return 0.0; }).term == Term("a"));
    throws<std::invalid_argument>([&] { cyclic.extract(a, [](const ENode&) { return -1.0; }); });
    auto matches = search(cyclic, parse("(f (f ?x))")); CHECK(matches.matches.size() == 1);

    EGraph costs; auto long_form = costs.add(parse("(slow x)")), short_form = costs.add(Term("fast")); costs.merge(long_form, short_form);
    CHECK(costs.extract(long_form, [](const ENode& n) { return n.op == "fast" ? 10.0 : 1.0; }).term == parse("(slow x)"));

    Saturator growing; growing.add_rule({"grow", parse("(f ?x)"), parse("(f (g ?x))")});
    EGraph bounded; bounded.add(parse("(f x)")); Limits limits; limits.nodes = 3;
    CHECK(growing.run(bounded, limits).stop == SaturationStop::NodeLimit); CHECK(bounded.allocated_nodes() == 3); CHECK(bounded.extract(0).term == Term("x"));
    EGraph iterations; iterations.add(Term("x")); limits = {}; limits.iterations = 0;
    CHECK(growing.run(iterations, limits).stop == SaturationStop::IterationLimit);
    EGraph capped; capped.add(parse("(pair (f x) (f y))")); limits = {}; limits.matches_per_rule = 1;
    CHECK(growing.run(capped, limits).stop == SaturationStop::MatchLimit);
    EGraph applications; applications.add(parse("(f x)")); limits = {}; limits.applications = 0;
    CHECK(growing.run(applications, limits).stop == SaturationStop::ApplicationLimit); CHECK(applications.allocated_nodes() == 2);
    Saturator guarded; guarded.add_rule({"safe", parse("(f ?x)"), parse("?x"), [](const EGraph& g, const ESubstitution& s) {
        for (const auto& n : g.nodes(s.at("?x"))) if (n.op == "safe" && n.children.empty()) return true;
        return false;
    }});
    EGraph guard_graph; auto safe = guard_graph.add(parse("(f safe)")), unsafe = guard_graph.add(parse("(f unsafe)"));
    CHECK(guarded.run(guard_graph).stop == SaturationStop::Saturated); CHECK(guard_graph.extract(safe).term == Term("safe")); CHECK(guard_graph.extract(unsafe).term == parse("(f unsafe)"));
}); }
