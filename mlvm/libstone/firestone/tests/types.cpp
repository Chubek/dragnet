#include "Test.hpp"

namespace ft = firestone::type;
using firestone::ir::Type;

int main() { return test_main([] {
    auto i8 = ft::Type::atomic(Type::integer(8)), i32 = ft::Type::atomic(Type::integer());
    auto record = ft::Type::record({{"tag", i8}, {"count", i32}, {"values", ft::Type::array(i8, 3)}}, "Packet");
    auto layout = ft::layout(record);
    CHECK(layout.offsets == std::vector<std::size_t>({0, 4, 8}) && layout.size == 12 && layout.alignment == 4);
    auto pointer = ft::Type::pointer(ft::Type::atomic(Type::void_type()));
    CHECK(ft::layout(pointer, {4, 4, 4}).size == 4);
    CHECK(ft::to_string(record).find("count") != std::string::npos);
    throws<std::overflow_error>([&] { ft::layout(ft::Type::array(i32, std::numeric_limits<std::size_t>::max())); });
    throws<std::invalid_argument>([&] { ft::Type::record({{"x", i8}, {"x", i32}}); });
    throws<std::invalid_argument>([&] { ft::layout(ft::Type::atomic(Type::memory())); });
    CHECK(ft::classify_conversion(Type::integer(8), Type::integer(32)) == ft::Conversion::IntegerWidening);
    CHECK(!ft::implicit_conversion(Type::integer(64), Type::floating(64)));

    auto variable = ft::Type::variable("T");
    auto left = ft::Type::function(variable, {ft::Type::pointer(variable)});
    auto right = ft::Type::function(i32, {ft::Type::pointer(i32)});
    auto unified = ft::unify(left, right);
    CHECK(unified && ft::apply_substitution(left, *unified) == right);
    CHECK(!ft::unify(variable, ft::Type::pointer(variable)));
    CHECK(!ft::unify(i8, i32));
    throws<std::invalid_argument>([&] { ft::apply_substitution(variable, {{"T", variable}}); });

    using F = ft::FType;
    auto identity = F::forall("A", F::arrow(F::variable("A"), F::variable("A")));
    CHECK(ft::alpha_equivalent(identity, F::forall("B", F::arrow(F::variable("B"), F::variable("B")))));
    CHECK(!ft::alpha_equivalent(identity, F::forall("B", F::arrow(F::variable("B"), F::variable("A")))));
    auto capture = ft::substitute(F::forall("B", F::arrow(F::variable("A"), F::variable("B"))), "A", F::variable("B"));
    CHECK(capture.name != "B" && ft::free_variables(capture) == std::set<std::string>{"B"});
    CHECK(ft::alpha_equivalent(capture, F::forall("C", F::arrow(F::variable("B"), F::variable("C")))));
    auto shadow = F::forall("A", F::forall("A", F::variable("A")));
    CHECK(ft::alpha_equivalent(shadow, F::forall("B", F::forall("C", F::variable("C")))));
    CHECK(ft::alpha_equivalent(ft::substitute(identity, "A", F::atomic(Type::integer())), identity));
    auto instantiated = ft::instantiate(identity, F::atomic(Type::integer()));
    CHECK(ft::alpha_equivalent(instantiated, F::arrow(F::atomic(Type::integer()), F::atomic(Type::integer()))));

    using E = ft::FTerm;
    auto expression = E::type_lambda("A", E::lambda("x", F::variable("A"), E::variable("x")));
    CHECK(ft::alpha_equivalent(ft::infer(expression), identity));
    auto applied = E::apply(E::type_apply(expression, F::atomic(Type::integer())), E::variable("input"));
    CHECK(ft::alpha_equivalent(ft::infer(applied, {{"input", F::atomic(Type::integer())}}), F::atomic(Type::integer())));
    throws<std::invalid_argument>([&] { ft::infer(applied, {{"input", F::atomic(Type::floating())}}); });
    throws<std::invalid_argument>([&] { ft::infer(E::type_lambda("A", E::variable("external")), {{"external", F::variable("A")}}); });
}); }
