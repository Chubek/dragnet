#include <Firestone.hpp>
#include <libir/Typed3AC.hpp>
#include <libeqsat/Saturation.hpp>

int main() {
    firestone::memory::Arena arena;
    firestone::memory::ArenaResource resource(arena);
    std::pmr::vector<int> values(&resource);
    values.push_back(42);
    auto layout = firestone::type::layout(firestone::type::Type::atomic(firestone::ir::Type::integer()));
    firestone::object::ClassHierarchy classes;
    auto base = classes.create("Consumer");
    classes.add_field(base, "value", firestone::type::Type::atomic(firestone::ir::Type::integer()));
    if (firestone::object::object_layout(classes, base).size != layout.size || values.front() != 42) return 1;
    firestone::cps::CPSBuilder builder;
    auto entry = builder.create("entry");
    builder.graph().halt(entry, firestone::ir::Operand::constant(42));
    auto lowered = firestone::cps::lower_to_ssa(builder.finish());
    if (!firestone::sema::check_types(lowered.ssa).empty()) return 1;
    firestone::eqsat::EGraph graph;
    auto root = graph.add(firestone::rewrite::parse("(add x 0)"));
    firestone::eqsat::Saturator runner;
    runner.add_rule({"identity", firestone::rewrite::parse("(add ?x 0)"), firestone::rewrite::parse("?x")});
    auto result = runner.run(graph);
    return result.stop == firestone::eqsat::SaturationStop::Saturated && graph.extract(root).term.op == "x" ? 0 : 1;
}
