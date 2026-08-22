#pragma once

#include <regex>
#include <string>
#include <vector>

namespace me::database {

enum class TokenKind {
    Keyword,
    String,
    Number,
    Comment,
    Operator,
    Identifier,
    Whitespace,
    Unknown
};

struct HighlightToken {
    std::size_t start = 0;
    std::size_t length = 0;
    TokenKind kind = TokenKind::Unknown;
};

// Minimal deterministic SQL tokenizer used by the editor's Material Design 3
// syntax colors. It intentionally avoids external dependencies.
class SqlHighlighter {
public:
    std::vector<HighlightToken> tokenize(const std::string& sql) const;
    static bool isKeyword(const std::string& word);

private:
    static bool isSpace(char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }
    static bool isAlpha(char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    }
    static bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }
};

}  // namespace me::database
