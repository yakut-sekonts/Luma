#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace luma::detail {
class Statement {
  public:
    Statement(sqlite3 *db, const char *sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr) != SQLITE_OK)
            fail();
    }
    ~Statement() {
        sqlite3_finalize(statement_);
    }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    void text(int index, const std::string &value) {
        if (sqlite3_bind_text(statement_, index, value.c_str(), static_cast<int>(value.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK)
            fail();
    }
    void number(int index, std::optional<double> value) {
        if ((value ? sqlite3_bind_double(statement_, index, *value)
                   : sqlite3_bind_null(statement_, index)) != SQLITE_OK)
            fail();
    }
    void integer(int index, std::int64_t value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK)
            fail();
    }
    void blob(int index, std::span<const std::uint8_t> value) {
        if (value.size() > 8192 ||
            sqlite3_bind_blob(statement_, index, value.data(), static_cast<int>(value.size()),
                              SQLITE_TRANSIENT) != SQLITE_OK)
            fail();
    }
    std::vector<std::uint8_t> bytes(int column) const {
        const int size = sqlite3_column_bytes(statement_, column);
        const auto *value =
            static_cast<const std::uint8_t *>(sqlite3_column_blob(statement_, column));
        if (sqlite3_column_type(statement_, column) != SQLITE_BLOB || !value || size < 1 ||
            size > 8192)
            throw std::runtime_error("Invalid stored binary field.");
        return {value, value + size};
    }
    bool row() {
        const int code = sqlite3_step(statement_);
        if (code != SQLITE_ROW && code != SQLITE_DONE)
            fail();
        return code == SQLITE_ROW;
    }
    std::string string(int column) const {
        const auto *value = sqlite3_column_text(statement_, column);
        return value ? reinterpret_cast<const char *>(value) : "";
    }
    std::optional<double> optional(int column) const {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL
                   ? std::nullopt
                   : std::optional<double>(sqlite3_column_double(statement_, column));
    }
    double value(int column) const {
        return sqlite3_column_double(statement_, column);
    }
    std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }

  private:
    sqlite3 *db_;
    sqlite3_stmt *statement_ = nullptr;
    [[noreturn]] void fail() const {
        throw std::runtime_error(std::string("Catalog: ") + sqlite3_errmsg(db_));
    }
};
} // namespace luma::detail
