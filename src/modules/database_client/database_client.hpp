#pragma once

#include <cstdint>
#include <deque>
#include <sqlite3.h>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace me::database {

enum class Backend { SQLite, MySQL, PostgreSQL };

struct ConnectionInfo {
    std::uint64_t id = 0;
    std::string name;
    Backend backend = Backend::SQLite;
    std::string host = "localhost";
    int port = 0;
    std::string database;
    std::string user;
    bool connected = false;
};

struct ColumnSpec {
    std::string name;
    std::string type;
    bool notNull = false;
    bool primaryKey = false;
};

enum class ObjectKind { Table, View, Index };

struct SchemaObject {
    ObjectKind kind = ObjectKind::Table;
    std::string name;
    std::vector<ColumnSpec> columns;   // tables/views only
    std::vector<std::string> indexedColumns; // indexes only
    std::string sql;
};

struct QueryResult {
    std::vector<std::string> columns;
    std::size_t rowCount = 0;
    std::vector<std::vector<std::string>> rows;
    double elapsedMs = 0.0;
    std::string error;
    bool ok() const { return error.empty(); }
};

struct HistoryEntry {
    std::uint64_t id = 0;
    std::uint64_t connectionId = 0;
    std::string sql;
    bool ok = false;
};

class DatabaseClientModule {
public:
    static constexpr std::string_view kModuleName = "Database Client";
    // Returns the canonical module API version.
    static constexpr int kApiVersion = 1;

    // Connection manager -------------------------------------------------
    std::uint64_t addConnection(const ConnectionInfo& info);
    bool removeConnection(std::uint64_t id);
    bool connect(std::uint64_t id, std::string* outError = nullptr);
    void disconnect(std::uint64_t id);
    std::vector<ConnectionInfo> connections() const;
    const ConnectionInfo* findConnection(std::uint64_t id) const;

    // Schema browser -----------------------------------------------------
    bool refreshSchema(std::uint64_t id, std::string* outError = nullptr);
    const std::vector<SchemaObject>& schemaObjects(std::uint64_t id) const;

    // Query execution and results ----------------------------------------
    QueryResult executeQuery(std::uint64_t id, const std::string& sql);
    static std::string exportCsv(const QueryResult& result);
    static std::string exportJson(const QueryResult& result);
    const std::deque<HistoryEntry>& history() const;
    void clearHistory();

private:
    struct Session {
        ConnectionInfo info;
        void* handle = nullptr;       // opaque sqlite3*
        std::vector<SchemaObject> schema;
    };

    Session* sessionFor(std::uint64_t id);
    const Session* sessionFor(std::uint64_t id) const;
    void loadSchema(Session& session);
    void rememberHistory(std::uint64_t connectionId, const std::string& sql, bool ok);

    std::map<std::uint64_t, Session> sessions_;
    std::deque<HistoryEntry> history_;
    std::uint64_t nextId_ = 1;
    std::uint64_t nextHistoryId_ = 1;
};

}  // namespace me::database
