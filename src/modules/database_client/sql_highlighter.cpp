#include "sql_highlighter.hpp"

#include <algorithm>
#include <cctype>

namespace me::database {

namespace {

const std::vector<std::string>& keywords() {
    static const std::vector<std::string> words = {
        "ADD", "ALTER", "AND", "AS", "ASC", "BEGIN", "BETWEEN", "BY", "CASE",
        "COMMIT", "CONSTRAINT", "CREATE", "CROSS", "DELETE", "DESC", "DISTINCT",
        "DROP", "ELSE", "END", "EXCEPT", "EXISTS", "FROM", "FULL", "GROUP",
        "HAVING", "IN", "INDEX", "INNER", "INSERT", "INTERSECT", "INTO", "IS",
        "JOIN", "LEFT", "LIKE", "LIMIT", "NOT", "NULL", "OFFSET", "ON", "OR",
        "ORDER", "OUTER", "PRIMARY", "REFERENCES", "RIGHT", "ROLLBACK",
        "SELECT", "SET", "TABLE", "THEN", "TRANSACTION", "UNION", "UNIQUE",
        "UPDATE", "VALUES", "VIEW", "WHEN", "WHERE", "WITH"
    };
    return words;
}

}  // namespace

bool SqlHighlighter::isKeyword(const std::string& word) {
    const auto& list = keywords();
    return std::find(list.begin(), list.end(), word) != list.end();
}

std::vector<HighlightToken> SqlHighlighter::tokenize(const std::string& sql) const {
    std::vector<HighlightToken> tokens;
    std::size_t i = 0;
    while (i < sql.size()) {
        HighlightToken token;
        token.start = i;

        if (isSpace(sql[i])) {
            while (i < sql.size() && isSpace(sql[i])) ++i;
            token.kind = TokenKind::Whitespace;
        } else if (sql.compare(i, 2, "--") == 0) {
            while (i < sql.size() && sql[i] != '\n') ++i;
            if (i < sql.size()) ++i;
            token.kind = TokenKind::Comment;
        } else if (sql[i] == '\'' || sql[i] == '"') {
            const char quote = sql[i++];
            while (i < sql.size()) {
                if (sql[i] == quote) {
                    ++i;
                    if (i >= sql.size() || sql[i] != quote) break;
                }
                ++i;
            }
            token.kind = TokenKind::String;
        } else if (isDigit(sql[i]) ||
                   (sql[i] == '.' && i + 1 < sql.size() && isDigit(sql[i + 1]))) {
            bool seenDot = false;
            while (i < sql.size() &&
                   (isDigit(sql[i]) || (!seenDot && sql[i] == '.' && (seenDot = true)))) {
                ++i;
            }
            token.kind = TokenKind::Number;
        } else if (isAlpha(sql[i])) {
            std::string word;
            while (i < sql.size() && (isAlpha(sql[i]) || isDigit(sql[i]))) {
                word.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i]))));
                ++i;
            }
            token.kind = isKeyword(word) ? TokenKind::Keyword : TokenKind::Identifier;
        } else {
            ++i;
            token.kind = TokenKind::Operator;
        }

        token.length = i - token.start;
        tokens.push_back(token);
    }
    return tokens;
}

}  // namespace me::database
