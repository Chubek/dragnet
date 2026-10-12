#pragma once
#include <memory>
#include <string>
#include <variant>

namespace dragscript {
class Value {
public:
    enum class Tag { undefined, null, boolean, number, string };
    Value() = default;
    static Value null();
    explicit Value(bool value) : data_(value) {}
    explicit Value(double value) : data_(value) {}
    explicit Value(std::u16string value);
    // Named factory: Value(u"text") would prefer the bool constructor through a pointer conversion.
    static Value text(std::u16string value) { return Value(std::move(value)); }
    [[nodiscard]] Tag tag() const noexcept;
    [[nodiscard]] bool boolean() const; // Accessors throw bad_variant_access on wrong tag.
    [[nodiscard]] double number() const;
    [[nodiscard]] const std::u16string& string() const;
    [[nodiscard]] bool truthy() const noexcept;
    [[nodiscard]] std::string display() const; // Debug/host formatting, not JS ToString.
    [[nodiscard]] bool strictly_equals(const Value& other) const noexcept;
private:
    struct Null {};
    std::variant<std::monostate, Null, bool, double, std::shared_ptr<const std::u16string>> data_;
};
} // namespace dragscript
