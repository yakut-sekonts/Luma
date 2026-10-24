#include "core/library/SmartCollection.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace luma {
namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error("Invalid smart collection rule.");
}
std::string column(const std::string &field) {
    if (field == "rating")
        return "a.rating";
    if (field == "flag")
        return "a.flag";
    if (field == "iso")
        return "a.iso";
    if (field == "width")
        return "a.width";
    if (field == "height")
        return "a.height";
    if (field == "camera")
        return "a.camera";
    if (field == "lens")
        return "a.lens";
    if (field == "filename")
        return "a.filename";
    if (field == "captureDate")
        return "strftime('%Y-%m-%d',a.capture_date,'unixepoch')";
    if (field == "label")
        return "COALESCE(a.color_label,'')";
    if (field == "keyword")
        return "k.keyword";
    throw std::runtime_error("Unsupported smart collection field.");
}
bool numeric(const std::string &field) {
    return field == "rating" || field == "flag" || field == "iso" || field == "width" ||
           field == "height";
}
void validateRule(const SmartRule &r, int depth, int &count) {
    require(++count <= 64 && depth <= 8);
    if (r.kind == "all" || r.kind == "any" || r.kind == "not") {
        require(!r.children.empty() && r.children.size() <= 16 &&
                (r.kind != "not" || r.children.size() == 1));
        for (const auto &c : r.children)
            validateRule(c, depth + 1, count);
    } else {
        require(r.kind == "condition" && r.children.empty());
        (void)column(r.field);
        require(r.operation == "eq" || r.operation == "ne" || r.operation == "gt" ||
                r.operation == "ge" || r.operation == "lt" || r.operation == "le" ||
                r.operation == "contains");
        if (numeric(r.field)) {
            require(std::holds_alternative<double>(r.value) && r.operation != "contains");
            const double v = std::get<double>(r.value);
            require(std::isfinite(v));
            require(r.field == "flag"     ? v >= -1 && v <= 1
                    : r.field == "rating" ? v >= 0 && v <= 5
                                          : v >= 0 && v <= 1e9);
        } else {
            require(std::holds_alternative<std::string>(r.value));
            const auto &v = std::get<std::string>(r.value);
            require((!v.empty() || r.operation == "eq" || r.operation == "ne") &&
                    v.size() <= 1024 && v.find('\0') == std::string::npos);
        }
    }
}
void write(std::ostream &out, const SmartRule &r) {
    out << std::quoted(r.kind) << ' ';
    if (r.kind == "condition") {
        out << std::quoted(r.field) << ' ' << std::quoted(r.operation) << ' ';
        if (numeric(r.field))
            out << std::get<double>(r.value);
        else
            out << std::quoted(std::get<std::string>(r.value));
    } else {
        out << r.children.size();
        for (const auto &c : r.children) {
            out << ' ';
            write(out, c);
        }
    }
}
SmartRule read(std::istream &in, int depth, int &count) {
    require(++count <= 64 && depth <= 8);
    SmartRule r;
    in >> std::quoted(r.kind);
    if (r.kind == "condition") {
        in >> std::quoted(r.field) >> std::quoted(r.operation);
        if (numeric(r.field)) {
            double v = 0;
            in >> v;
            r.value = v;
        } else {
            std::string v;
            in >> std::quoted(v);
            r.value = v;
        }
    } else {
        require(r.kind == "all" || r.kind == "any" || r.kind == "not");
        unsigned n = 0;
        in >> n;
        require(n > 0 && n <= 16);
        for (unsigned i = 0; i < n; ++i)
            r.children.push_back(read(in, depth + 1, count));
    }
    require(static_cast<bool>(in));
    return r;
}
std::string compile(const SmartRule &r, SmartSQL &q, int first) {
    if (r.kind != "condition") {
        std::string result = r.kind == "not" ? "(NOT " : "(";
        for (std::size_t i = 0; i < r.children.size(); ++i) {
            if (i)
                result += r.kind == "all" ? " AND " : " OR ";
            result += compile(r.children[i], q, first);
        }
        return result + ")";
    }
    auto value = r.value;
    const std::string op = (r.operation == "eq" || (r.field == "keyword" && r.operation == "ne"))
                               ? "="
                           : r.operation == "ne" ? "!="
                           : r.operation == "gt" ? ">"
                           : r.operation == "ge" ? ">="
                           : r.operation == "lt" ? "<"
                           : r.operation == "le" ? "<="
                                                 : " LIKE ";
    if (r.operation == "contains") {
        std::string escaped = "%";
        for (char c : std::get<std::string>(value)) {
            if (c == '%' || c == '_' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        value = escaped + "%";
    }
    const auto index = first + static_cast<int>(q.values.size());
    q.values.push_back(value);
    auto expr = column(r.field) + op + "?" + std::to_string(index) +
                (r.operation == "contains" ? " ESCAPE '\\'" : "");
    if (r.field == "keyword")
        expr = (r.operation == "ne"
                    ? "NOT EXISTS(SELECT 1 FROM asset_keywords k WHERE k.asset_id=a.id AND "
                    : "EXISTS(SELECT 1 FROM asset_keywords k WHERE k.asset_id=a.id AND ") +
               expr + ")";
    return "COALESCE((" + expr + "),0)";
}
} // namespace
void SmartRule::validate() const {
    int count = 0;
    validateRule(*this, 0, count);
}
std::string encodeSmartRule(const SmartRule &rule) {
    rule.validate();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "LUMA_SMART 1 " << std::setprecision(17);
    write(out, rule);
    out << '\n';
    return out.str();
}
SmartRule decodeSmartRule(const std::string &text) {
    require(!text.empty() && text.size() <= 65536);
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    in >> magic >> version;
    require(magic == "LUMA_SMART" && version == 1);
    int count = 0;
    auto result = read(in, 0, count);
    result.validate();
    require(encodeSmartRule(result) == text);
    return result;
}
SmartSQL compileSmartRule(const SmartRule &rule, int first) {
    rule.validate();
    require(first >= 1 && first <= 100);
    SmartSQL result;
    result.expression = compile(rule, result, first);
    return result;
}
} // namespace luma
