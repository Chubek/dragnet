#include "Test.hpp"

using namespace firestone::object;
using firestone::ir::Type;

int main() { return test_main([] {
    ClassHierarchy hierarchy;
    auto base = hierarchy.create("Base");
    hierarchy.add_field(base, "tag", firestone::type::Type::atomic(Type::integer(8)));
    auto slot = hierarchy.define_method(base, {"compute", "Base.compute", Type::integer(), {Type::integer()}});
    auto derived = hierarchy.create("Derived", base);
    CHECK(hierarchy.is_subclass(derived, base) && !hierarchy.is_subclass(base, derived));
    throws<std::logic_error>([&] { hierarchy.add_field(base, "later", firestone::type::Type::atomic(Type::integer())); });
    CHECK(hierarchy.define_method(derived, {"compute", "Derived.compute", Type::integer(), {Type::integer()}}) == slot);
    throws<std::invalid_argument>([&] { hierarchy.define_method(derived, {"compute", "bad", Type::floating(), {Type::integer()}}); });
    throws<std::invalid_argument>([&] { hierarchy.add_field(derived, "tag", firestone::type::Type::atomic(Type::integer())); });
    hierarchy.add_field(derived, "number", firestone::type::Type::atomic(Type::integer()));
    auto layout = object_layout(hierarchy, derived);
    CHECK(layout.vptr_offset == 0 && layout.offsets.at("tag") == 8 && layout.offsets.at("number") == 12 && layout.size == 16);
    CHECK(object_layout(hierarchy, derived, {4, 4, 4}).size == 12);

    ObjectGraph objects;
    auto a = objects.create("Derived"), b = objects.create("Base");
    objects.set(a, "next", ObjectRef{b}); objects.set(a, "also", ObjectRef{b}); objects.set(b, "next", ObjectRef{a});
    objects.set(a, "number", std::int64_t{42});
    CHECK(objects.reachable(a).size() == 2);
    auto target = dispatch(hierarchy, objects, a, "compute", {Type::integer()});
    CHECK(target.slot == slot && target.method.target == "Derived.compute");
    throws<std::invalid_argument>([&] { dispatch(hierarchy, objects, a, "compute", {Type::floating()}); });
    throws<std::logic_error>([&] { objects.remove(b); });
    auto cloned = clone_reachable(objects, a, objects);
    CHECK(cloned.root != a && objects.reachable(cloned.root).size() == 2);
    CHECK(std::get<ObjectRef>(objects.field(cloned.root, "next")).id == cloned.mapping.at(b));
    CHECK(std::get<ObjectRef>(objects.field(cloned.mapping.at(b), "next")).id == cloned.root);
    CHECK(std::get<ObjectRef>(objects.field(cloned.root, "next")) == std::get<ObjectRef>(objects.field(cloned.root, "also")));
    std::size_t visits = 0; visit_reachable(objects, cloned.root, [&](ObjectId, const Object&) { ++visits; }); CHECK(visits == 2);
    objects.erase_field(a, "next"); CHECK(objects.reachable(a).size() == 2);
    objects.erase_field(a, "also"); objects.remove(b); CHECK(!objects.contains(b));
    throws<std::out_of_range>([&] { objects.set(a, "dangling", ObjectRef{b}); });
    CHECK(objects.get(a).fields.count("dangling") == 0);
}); }
