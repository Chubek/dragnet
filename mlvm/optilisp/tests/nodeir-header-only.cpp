#include <mlvm/NodeIR/NodeHIR.hpp>
#include <mlvm/NodeIR/NodeMIR.hpp>
#include <mlvm/NodeIR/NodeLIR.hpp>

// This executable links no project library: all NodeIR implementation is inline.
int main() {
    using namespace mlvm::nodeir;
    NodeHIR ir;
    auto x = ir.literal(Kind::number, "7");
    ir.roots({ir.operation("and", {x, x})});
    ir.verify();
    Bindings bindings;
    auto pattern = Pattern::operation("and", {Pattern::bind("x"), Pattern::bind("x")});
    if (!match(pattern, ir, ir.roots()[0], bindings)) return 1;
    auto replacement = instantiate(Pattern::bind("x"), ir, bindings);
    ir.replace(ir.roots()[0], replacement);
    auto foreign = translate_from<std::string>(ir, [](const NodeHIR& value) { return value.at(value.roots()[0]).value; });
    return foreign.ok() && *foreign.value == "7" ? 0 : 1;
}
