#include "Test.hpp"

namespace rw = firestone::rewrite;
namespace eq = firestone::eqsat;

int main() { return test_main([] {
    auto term = rw::parse("(f (g x) (g x))");
    CHECK(term.depth() == 3 && rw::positions(term).size() == 5);
    CHECK(rw::term_at(term, {1, 0}).op == "x");
    CHECK(rw::to_string(rw::replace_at(term, {0}, rw::parse("y"))) == "(f y (g x))");
    throws<std::out_of_range>([&] { rw::term_at(term, {5}); });
    rw::Substitution bindings{{"?existing", rw::parse("a")}};
    CHECK(!rw::match_into(rw::parse("(f ?x ?x)"), rw::parse("(f a b)"), bindings));
    CHECK(bindings.size() == 1);
    auto unified = rw::unify(rw::parse("(f ?x (g ?y))"), rw::parse("(f (g a) (g b))"));
    CHECK(unified && rw::apply_substitution(rw::parse("?x"), *unified) == rw::parse("(g a)"));
    CHECK(!rw::unify(rw::parse("?x"), rw::parse("(f ?x)")));
    throws<std::invalid_argument>([] { rw::apply_substitution(rw::parse("?x"), {{"?x", rw::parse("?y")}, {"?y", rw::parse("?x")}}); });
    rw::Rule outer{"outer", rw::parse("(f (g ?x))"), rw::parse("a")};
    rw::Rule inner{"inner", rw::parse("(g ?x)"), rw::parse("b")};
    auto pairs = rw::critical_pairs(outer, inner);
    CHECK(pairs.size() == 1 && pairs[0].overlap == rw::Position{0});
    CHECK(pairs[0].left.op == "a" && pairs[0].right == rw::parse("(f b)"));
    rw::Rule one{"one", rw::parse("(mul ?x 1)"), rw::parse("?x")};
    rw::Rule zero{"zero", rw::parse("(add ?x 0)"), rw::parse("?x")};
    auto strategy = rw::bottom_up(rw::choice(rw::rule_strategy(one), rw::rule_strategy(zero)));
    auto normalized = rw::repeat(strategy, rw::parse("(add (mul x 1) 0)"));
    CHECK(normalized.normal_form && normalized.term.op == "x" && normalized.steps == 1);
    auto cycle = rw::choice(rw::rule_strategy({"a-b", rw::parse("a"), rw::parse("b")}), rw::rule_strategy({"b-a", rw::parse("b"), rw::parse("a")}));
    CHECK(!rw::repeat(cycle, rw::parse("a"), 3).normal_form);
    rw::Rewriter rewriter; rewriter.add_rule(outer); rewriter.add_rule(inner);
    CHECK(rewriter.run(rw::parse("(f (g x))"), 1, 100, rw::RewriteOrder::TopDown).term.op == "a");
    CHECK(rewriter.run(rw::parse("(f (g x))"), 1).term == rw::parse("(f b)"));

    eq::EGraph graph;
    auto root = graph.add(rw::parse("(pair (f x) (f x))"));
    auto dag = eq::extract_dag(graph, root);
    CHECK(dag.nodes.size() == 3 && dag.tree_cost == 5 && eq::expand(dag) == rw::parse("(pair (f x) (f x))"));
    CHECK(dag.nodes[dag.root].children[0] == dag.nodes[dag.root].children[1]);
    eq::CostModel costs(1); costs.set("pair", 5); CHECK(graph.extract(root, costs).cost == 9);
    throws<std::invalid_argument>([&] { costs.set("x", -1); });
    auto leaf = graph.lookup(eq::ENode{"x", {}}); CHECK(leaf && graph.contains(*leaf));
    CHECK(graph.reachable_classes(root).size() == 3);
    CHECK(eq::statistics(graph).edges == 3 && eq::to_dot(graph).find("pair") != std::string::npos);
    graph.merge(*leaf, graph.add(eq::ENode{"cycle", {*leaf}})); graph.rebuild();
    using Data = std::set<std::string>;
    auto make = [](const eq::ENode& node, const std::vector<Data>& children) { Data data{node.op}; for (const auto& child : children) data.insert(child.begin(), child.end()); return data; };
    auto join = [](Data left, const Data& right) { left.insert(right.begin(), right.end()); return left; };
    auto analysis = eq::analyze(graph, Data{}, make, join);
    CHECK(analysis.converged && analysis.at(graph, root) == Data({"pair", "f", "x", "cycle"}));
    CHECK(!eq::analyze(graph, Data{}, make, join, 0).converged);
    graph.add(rw::parse("new"));
    throws<std::logic_error>([&] { analysis.at(graph, root); });
}); }
