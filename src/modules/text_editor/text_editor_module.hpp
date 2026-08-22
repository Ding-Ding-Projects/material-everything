#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace me::text_editor {

enum class Language {
    PlainText,
    Cpp,
    JavaScript,
    Python,
    Json,
    Markdown,
};

enum class TokenType {
    Text,
    Keyword,
    String,
    Comment,
    Number,
    Punctuation,
};

struct Token {
    size_t line = 0;
    size_t column = 0;
    size_t length = 0;
    TokenType type = TokenType::Text;
};

struct SearchMatch {
    size_t start = 0;
    size_t length = 0;
};

// A self-contained tabbed editor model with syntax highlighting, search and
// replace, auto-indentation and file persistence. The UI layer renders this
// state as Material Design 3 tabs, a code surface and an editor toolbar.
class TextEditorModule {
public:
    struct Document {
        std::string id;
        std::string title;
        std::string path;
        std::string content;
        Language language = Language::PlainText;
        bool dirty = false;
        bool readOnly = false;
        int cursorLine = 1;
        int cursorColumn = 1;
    };

    Document& createDocument(const std::string& title);
    std::optional<std::string> openFile(const std::string& path);
    bool saveDocument(const std::string& id);
    bool saveDocumentAs(const std::string& id, const std::string& path);
    void closeDocument(const std::string& id);

    std::vector<Document>& documents();
    const std::vector<Document>& documents() const;
    Document* document(const std::string& id);
    const Document* document(const std::string& id) const;
    const std::string& activeDocumentId() const;
    void setActiveDocumentId(const std::string& id);

    void insertText(const std::string& id, const std::string& text);
    void replaceRange(const std::string& id, size_t offset, size_t length,
                      const std::string& replacement);
    void deleteBackward(const std::string& id);
    void newlineWithAutoIndent(const std::string& id);
    void setCursor(const std::string& id, int line, int column);

    static Language detectLanguage(const std::string& fileNameOrContent);
    std::vector<Token> highlight(const Document& doc) const;
    std::vector<std::string> lines(const Document& doc) const;

    std::vector<SearchMatch> find(const Document& doc, const std::string& needle,
                                  bool caseSensitive = false) const;
    size_t replaceAll(const std::string& id, const std::string& needle,
                      const std::string& replacement, bool caseSensitive = false);
    std::vector<size_t> matchingBraceLines(const Document& doc) const;

private:
    Document* findDoc(const std::string& id);
    static std::string nextId();
    static bool isKeyword(Language lang, const std::string& word);
    static bool isNumberStart(char c);
    static bool isIdentifierChar(char c);

    std::vector<Document> docs_;
    std::string active_;
};

}  // namespace me::text_editor
