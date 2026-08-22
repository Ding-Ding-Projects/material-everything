#include "database_client.hpp"

#include <chrono>

namespace me::database {

namespace {

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start).count();
}

}  // namespace

std::uint64_t DatabaseClientModule::addConnection(const ConnectionInfo& info) {
    Session session;
    session.info = info;
    const std::uint64_t id = nextId_++;
    session.info.id = id;
    sessions_.emplace(id, std::move(session));
    return id;
}

bool DatabaseClientModule::removeConnection(std::uint64_t id) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    if (it->second.handle) sqlite3_close(static_cast<sqlite3*>(it->second.handle));
    sessions_.erase(it);
    return true;
}

bool DatabaseClientModule::connect(std::uint64_t id, std::string* outError) {
    Session* session = sessionFor(id);
    if (!session) {
        if (outError) *outError = "unknown connection";
        return false;
    }
    disconnect(id);
    if (session->info.backend != Backend::SQLite) {
        // MySQL and PostgreSQL are abstracted but not linked in this build.
        if (outError) *outError = "driver unavailable for backend";
        return false;
    }
    sqlite3* db = nullptr;
    const int rc = sqlite3_open(session->info.database.c_str(), &db);
    if (rc != SQLITE_OK) {
        if (outError && db) *outError = sqlite3_errmsg(db);
        else if (outError) *outError = sqlite3_errstr(rc);
        if (db) sqlite3_close(db);
        return false;
    }
    sqlite3_busy_timeout(db, 3000);
    session->handle = db;
    session->info.connected = true;
    loadSchema(*session);
    return true;
}

void DatabaseClientModule::disconnect(std::uint64_t id) {
    Session* session = sessionFor(id);
    if (!session || !session->handle) return;
    sqlite3_close(static_cast<sqlite3*>(session->handle));
    session->handle = nullptr;
    session->info.connected = false;
    session->schema.clear();
}

std::vector<ConnectionInfo> DatabaseClientModule::connections() const {
    std::vector<ConnectionInfo> result;
    result.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) result.push_back(session.info);
    return result;
}

const ConnectionInfo* DatabaseClientModule::findConnection(std::uint64_t id) const {
    const Session* session = sessionFor(id);
    return session ? &session->info : nullptr;
}

DatabaseClientModule::Session*
DatabaseClientModule::sessionFor(std::uint64_t id) {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

const DatabaseClientModule::Session*
DatabaseClientModule::sessionFor(std::uint64_t id) const {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

void DatabaseClientModule::loadSchema(Session& session) {
    session.schema.clear();
    auto* db = static_cast<sqlite3*>(session.handle);
    if (!db) return;
    auto addObjects = [&](ObjectKind kind, const char* sqlStatement) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sqlStatement, -1, &stmt, nullptr) != SQLITE_OK) return;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SchemaObject object;
            object.kind = kind;
            object.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (const unsigned char* sqlText = sqlite3_column_text(stmt, 1))
                object.sql = reinterpret_cast<const char*>(sqlText);

            std::string detailSql = "PRAGMA table_info(" + object.name + ")";
            sqlite3_stmt* detailStmt = nullptr;
            if (kind == ObjectKind::Index) {
                std::string indexInfo = "PRAGMA index_info(" + object.name + ")";
                sqlite3_stmt* idx = nullptr;
                if (sqlite3_prepare_v2(db, indexInfo.c_str(), -1, &idx, nullptr) == SQLITE_OK) {
                    while (sqlite3_step(idx) == SQLITE_ROW) {
                        if (const unsigned char* col = sqlite3_column_text(idx, 2))
                            object.indexedColumns.emplace_back(reinterpret_cast<const char*>(col));
                    }
                    sqlite3_finalize(idx);
                }
            } else if (sqlite3_prepare_v2(db, detailSql.c_str(), -1, &detailStmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(detailStmt) == SQLITE_ROW) {
                    ColumnSpec spec;
                    spec.name = reinterpret_cast<const char*>(sqlite3_column_text(detailStmt, 1));
                    if (const unsigned char* type = sqlite3_column_text(detailStmt, 2))
                        spec.type = reinterpret_cast<const char*>(type);
                    spec.notNull = sqlite3_column_int(detailStmt, 3) != 0;
                    spec.primaryKey = sqlite3_column_int(detailStmt, 5) != 0;
                    object.columns.push_back(spec);
                }
                sqlite3_finalize(detailStmt);
            }
            session.schema.push_back(std::move(object));
        }
        sqlite3_finalize(stmt);
    };

    addObjects(ObjectKind::Table,
               "SELECT name, sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name");
    addObjects(ObjectKind::View,
               "SELECT name, sql FROM sqlite_master WHERE type='view' ORDER BY name");
    addObjects(ObjectKind::Index,
               "SELECT name, sql FROM sqlite_master WHERE type='index' AND sql IS NOT NULL ORDER BY name");
}

bool DatabaseClientModule::refreshSchema(std::uint64_t id, std::string* outError) {
    Session* session = sessionFor(id);
    if (!session || !session->handle) {
        if (outError) *outError = "connection is not open";
        return false;
    }
    loadSchema(*session);
    return true;
}

const std::vector<SchemaObject>& DatabaseClientModule::schemaObjects(std::uint64_t id) const {
    static const std::vector<SchemaObject> empty;
    const Session* session = sessionFor(id);
    return session ? session->schema : empty;
}

QueryResult DatabaseClientModule::executeQuery(std::uint64_t id, const std::string& sql) {
    QueryResult result;
    Session* session = sessionFor(id);
    if (!session || !session->handle) {
        result.error = "connection is not open";
        rememberHistory(id, sql, false);
        return result;
    }
    auto* db = static_cast<sqlite3*>(session.handle);
    const auto start = std::chrono::steady_clock::now();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        result.error = sqlite3_errmsg(db);
        result.elapsedMs = msSince(start);
        rememberHistory(id, sql, false);
        return result;
    }
    const int columnCount = sqlite3_column_count(stmt);
    for (int i = 0; i < columnCount; ++i)
        result.columns.emplace_back(sqlite3_column_name(stmt, i));

    bool stepped = false;
    while ((stepped = (sqlite3_step(stmt) == SQLITE_ROW))) {
        std::vector<std::string> row;
        row.reserve(static_cast<std::size_t>(columnCount));
        for (int i = 0; i < columnCount; ++i) {
            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_NULL: row.emplace_back("NULL"); break;
                case SQLITE_FLOAT: row.push_back(std::to_string(sqlite3_column_double(stmt, i))); break;
                case SQLITE_INTEGER: row.push_back(std::to_string(sqlite3_column_int64(stmt, i))); break;
                default: {
                    const unsigned char* text = sqlite3_column_text(stmt, i);
                    row.emplace_back(text ? reinterpret_cast<const char*>(text) : "");
                    break;
                }
            }
        }
        result.rows.push_back(std::move(row));
    }
    if (!stepped && sqlite3_finalize(stmt) != SQLITE_OK)
        result.error = sqlite3_errmsg(db);
    stmt = nullptr;
    result.rowCount = result.rows.size();
    result.elapsedMs = msSince(start);
    rememberHistory(id, sql, result.ok());
    return result;
}

std::string DatabaseClientModule::exportCsv(const QueryResult& result) {
    auto field = [](std::string value) {
        if (value.find_first_of(",\"\n\r") != std::string::npos) {
            std::string escaped = "\"";
            for (char ch : value) {
                if (ch == '"') escaped += '"';
                escaped += ch;
            }
            escaped += '"';
            return escaped;
        }
        return value;
    };
    std::string csv;
    for (const auto& name : result.columns) csv += field(name) + ",";
    if (!result.columns.empty()) csv.pop_back();
    csv += "\r\n";
    for (const auto& row : result.rows) {
        for (const auto& cell : row) csv += field(cell) + ",";
        csv.pop_back();
        csv += "\r\n";
    }
    return csv;
}

namespace {

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[ch >> 4];
                    escaped += hex[ch & 15];
                } else {
                    escaped += static_cast<char>(ch);
                }
        }
    }
    return escaped;
}

}  // namespace

std::string DatabaseClientModule::exportJson(const QueryResult& result) {
    std::string json = "{\"columns\":[";
    for (std::size_t c = 0; c < result.columns.size(); ++c) {
        if (c) json += ',';
        json += '"' + jsonEscape(result.columns[c]) + '"';
    }
    json += "],\"rows\":[";
    for (std::size_t r = 0; r < result.rows.size(); ++r) {
        if (r) json += ',';
        json += '[';
        for (std::size_t c = 0; c < result.rows[r].size(); ++c) {
            if (c) json += ',';
            json += '"' + jsonEscape(result.rows[r][c]) + '"';
        }
        json += ']';
    }
    json += "],\"rowCount\":" + std::to_string(result.rowCount) + "}";
    return json;
}

const std::deque<HistoryEntry>& DatabaseClientModule::history() const {
    return history_;
}

void DatabaseClientModule::clearHistory() {
    history_.clear();
}

void DatabaseClientModule::rememberHistory(std::uint64_t connectionId,
                                            const std::string& sql, bool ok) {
    if (sql.empty()) return;
    HistoryEntry entry;
    entry.id = nextHistoryId_++;
    entry.connectionId = connectionId;
    entry.sql = sql;
    entry.ok = ok;
    constexpr std::size_t kMaxHistoryEntries = 200;
    if (history_.size() >= kMaxHistoryEntries) history_.pop_front();
    history_.push_back(std::move(entry));
}

}  // namespace me::database
