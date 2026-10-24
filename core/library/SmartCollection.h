#pragma once
#include <string>
#include <variant>
#include <vector>
namespace luma {
using SmartValue = std::variant<double, std::string>;
struct SmartRule {
    std::string kind = "all", field = "rating", operation = "ge";
    SmartValue value = 0.0;
    std::vector<SmartRule> children;
    void validate() const;
};
struct SmartSQL {
    std::string expression;
    std::vector<SmartValue> values;
};
std::string encodeSmartRule(const SmartRule &rule);
SmartRule decodeSmartRule(const std::string &text);
SmartSQL compileSmartRule(const SmartRule &rule, int firstParameter = 9);
} // namespace luma
