#include "text_editor_module.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace me::text_editor {
namespace {

const std::map<Language, std::vector<std::string>>& keywordTable() {
    static const std::map<Language, std::vector<std::string>> table = {
        {Language::Cpp,
         {"alignas", "auto", "bool", "break", "case", "catch", "char", "class",
          "const", "constexpr", "continue", "default", "delete", "do", "double",
          "else", "enum", "explicit", "export", "extern", "false", "float", "for",
          "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
          "new", "noexcept", "nullptr", "operator", "private", "protected", "public",
          "return", "short", "signed", "sizeof", "static", "struct", "switch",
          "template", "this", "throw", "true", "try", "typedef", "typename", "union",
          "unsigned", "using", "virtual", "void", "volatile", "while"}},
        {Language::JavaScript,
         {"async", "await", "break", "case", "catch", "class", "const", "continue",
          "default", "delete", "do", "else", "export", "extends", "false", "finally",
          "for", "function", "if", "import", "in", "instanceof", "let", "new", "null",
          "of", "return", "super", "switch", "this", "throw", "true", "try", "typeof",
          "undefined", "var", "while", "yield"}},
        {Language::Python,
         {"and", "as", "assert", "async", "await", "break", "class", "continue",
          "def", "del", "elif", "else", "except", "False", "finally", "for", "from",
          "global", "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
          "or", "pass", "raise", "return", "True", "try", "while", "with", "yield"}}};
    return table;
}

std::string extensionOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool containsAll(std::string_view haystack, std::initializer_list<const char*> needles) {
    for (const char* n : needles) {
        if (haystack.find(n) == std::string_view::npos) return false;
    }
    return true;
}

}  // namespace

TextEditorModule::Document& TextEditorModule::createDocument(const std::string& title) {
    Document d;
    d.id = nextId();
    d.title = title.empty() ? "Untitled" : title;
    docs_.push_back(std::move(d));
    active_ = docs_.back().id;
    return docs_.back();
}

std::optional<std::string> TextEditorModule::openFile(const std::string& path) {
    if (path.empty()) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;

    std::ostringstream ss;
    ss << in.rdbuf();
    Document d;
    d.id = nextId();
    d.path = path;
    d.content = ss.str();
    const size_t slash = path.find_last_of("/\\");
    d.title = slash == std::string::npos ? path : path.substr(slash + 1);
    d.language = detectLanguage(d.title.empty() ? path : d.title);
    docs_.push_back(std::move(d));
    active_ = docs_.back().id;
    return active_;
}

bool TextEditorModule::saveDocumentAs(const std::string& id, const std::string& path) {
    Document* d = findDoc(id);
    if (!d || path.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << d->content;
    if (!out.good()) return false;
    d->path = path;
    const size_t slash = path.find_last_of("/\\");
    d->title = slash == std::string::npos ? path : path.substr(slash + 1);
    d->language = detectLanguage(d->title);
    d->dirty = false;
    return true;
}

bool TextEditorModule::saveDocument(const std::string& id) {
    const Document* d = document(id);
    if (!d || d->path.empty()) return false;
    return saveDocumentAs(id, d->path);
}

void TextEditorModule::closeDocument(const std::string& id) {
    auto it = std::remove_if(docs_.begin(), docs_.end(),
                             [&](const Document& d) { return d.id == id; });
    if (it == docs_.end()) return;
    docs_.erase(it, docs_.end());
    if (active_ == id) active_ = docs_.empty() ? "" : docs_.front().id;
}

std::vector<TextEditorModule::Document>& TextEditorModule::documents() { return docs_; }
const std::vector<TextEditorModule::Document>& TextEditorModule::documents() const { return docs_; }

TextEditorModule::Document* TextEditorModule::document(const std::string& id) { return findDoc(id); }
const TextEditorModule::Document* TextEditorModule::document(const std::string& id) const {
    return const_cast<TextEditorModule*>(this)->findDoc(id);
}
const std::string& TextEditorModule::activeDocumentId() const { return active_; }

void TextEditorModule::setActiveDocumentId(const std::string& id) {
    if (findDoc(id)) active_ = id;
}

TextEditorModule::Document* TextEditorModule::findDoc(const std::string& id) {
    auto it = std::find_if(docs_.begin(), docs_.end(),
                           [&](const Document& d) { return d.id == id; });
    return it == docs_.end() ? nullptr : &*it;
}

std::string TextEditorModule::nextId() {
    static unsigned long long counter = 0;
    return "doc-" + std::to_string(++counter);
}

void TextEditorModule::insertText(const std::string& id, const std::string& text) {
    Document* d = findDoc(id);
    if (!d || d->readOnly) return;
    // Insert at the logical cursor when valid; otherwise append.
    size_t offset = 0;
    const auto ls = lines(*d);
    int l = std::clamp(d->cursorLine, 1, static_cast<int>(ls.size()));
    for (int i = 0; i < l - 1 && !ls.empty(); ++i) offset += ls[static_cast<size_t>(i)].size() + 1;
    const int col = std::max(1, d->cursorColumn) - 1;
    if (!ls.empty() && l >= 1) {
        const size_t lineLen = ls[static_cast<size_t>(l - 1)].size();
        offset += std::min(static_cast<size_t>(col), lineLen);
    }
    if (offset > d->content.size()) offset = d->content.size();
    d->content.insert(offset, text);
    d->dirty = true;
    setCursor(id, d->cursorLine, d->cursorColumn + static_cast<int>(text.size()));
}

void TextEditorModule::replaceRange(const std::string& id, size_t offset, size_t length,
                                    const std::string& replacement) {
    Document* d = findDoc(id);
    if (!d || d->readOnly) return;
    if (offset > d->content.size()) return;
    length = std::min(length, d->content.size() - offset);
    d->content.replace(offset, length, replacement);
    d->dirty = true;
}

void TextEditorModule::deleteBackward(const std::string& id) {
    Document* d = findDoc(id);
    if (!d || d->readOnly || d->content.empty()) return;
    const auto ls = lines(*d);
    int l = std::clamp(d->cursorLine, 1, static_cast<int>(ls.size()));
    size_t lineStart = 0;
    for (int i = 0; i < l - 1 && !ls.empty(); ++i) lineStart += ls[static_cast<size_t>(i)].size() + 1;
    const int col = std::max(1, d->cursorColumn);
    if (col > 1) {
        const size_t pos = lineStart + static_cast<size_t>(col - 2);
        if (pos < d->content.size()) d->content.erase(pos, 1);
        setCursor(id, d->cursorLine, std::max(1, d->cursorColumn - 1));
    } else if (l > 1) {
        // Join the previous line to this one.
        size_t prevNewline = d->content.rfind('\n', lineStart > 0 ? lineStart - 2 : 0);
        if (lineStart == 0) return;
        if (prevNewline != std::string::npos && prevNewline < lineStart)
            d->content.erase(prevNewline, 1);
        else
            d->content.erase(lineStart - 1, 1);
        setCursor(id, d->cursorLine - 1, 1);
    }
    d->dirty = true;
}

void TextEditorModule::newlineWithAutoIndent(const std::string& id) {
    Document* d = findDoc(id);
    if (!d || d->readOnly) return;
    const auto ls = lines(*d);
    int l = std::clamp(d->cursorLine, 1, static_cast<int>(ls.size()));
    std::string current = ls.empty() ? "" : ls[static_cast<size_t>(l - 1)];
    std::string indent;
    for (const char ch : current) {
        if (ch == ' ' || ch == '\t')
            indent.push_back(ch);
        else
            break;
    }
    const size_t firstNonSpace = current.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos &&
        (current[firstNonSpace] == '{' ||
         ((current[firstNonSpace] == ':' || current[firstNonSpace] == '(') &&
          d->language == Language::Python)))
        indent += "    ";
    insertText(id, "\n" + indent);
    setCursor(id, d->cursorLine + 1, static_cast<int>(indent.size()) + 1);
}

void TextEditorModule::setCursor(const std::string& id, int line, int column) {
    Document* d = findDoc(id);
    if (!d) return;
    const auto ls = lines(*d);
    d->cursorLine = std::clamp(line, 1, std::max<int>(1, static_cast<int>(ls.size())));
    const int width =
        ls.empty() ? 1 : std::max<int>(1, static_cast<int>(ls[static_cast<size_t>(d->cursorLine - 1)].size()) + 1);
    d->cursorColumn = std::clamp(column, 1, width);
}

Language TextEditorModule::detectLanguage(const std::string& fileNameOrContent) {
    std::string_view v(fileNameOrContent);
    const std::string ext = extensionOf(fileNameOrContent);
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "hpp" || ext == "h") return Language::Cpp;
    if (ext == "js" || ext == "jsx" || ext == "ts" || ext == "mjs") return Language::JavaScript;
    if (ext == "py") return Language::Python;
    if (ext == "json") return Language::Json;
    if (ext == "md" || ext == "markdown") return Language::Markdown;
    if (v.starts_with("{") || v.starts_with("[")) return Language::Json;
    if (containsAll(v, {"#include", ";"})) return Language::Cpp;
    if (containsAll(v, {"def ", ":"})) return Language::Python;
    if (containsAll(v, {"function", "}"})) return Language::JavaScript;
    if (v.find('#') == 0 || v.find("## ") != std::string_view::npos) return Language::Markdown;
    return Language::PlainText;
}

bool TextEditorModule::isKeyword(Language lang, const std::string& word) {
    const auto& table = keywordTable();
    auto it = table.find(lang);
    if (it == table.end()) return false;
    return std::find(it->second.begin(), it->second.end(), word) != it->second.end();
}

bool TextEditorModule::isNumberStart(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool TextEditorModule::isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::vector<Token> TextEditorModule::highlight(const Document& doc) const {
    std::vector<Token> out;
    if (doc.language == Language::Json || doc.language == Language::Markdown) {
        // JSON: strings and numbers. Markdown: headings and inline code spans.
        size_t pos = 0;
        while (pos < doc.content.size()) {
            const size_t nl = doc.content.find('\n', pos);
            const size_t end = nl == std::string::npos ? doc.content.size() : nl;
            const std::string_view sv(doc.content.data() + pos, end - pos);
            const size_t trimmed = sv.find_first_not_of(" \t");
            if (doc.language == Language::Json) {
                size_t p = 0;
                while (p < sv.size()) {
                    if (sv[p] == '"') {
                        size_t q = p + 1;
                        while (q < sv.size() && !(sv[q] == '"' && sv[q - 1] != '\\')) ++q;
                        Token t;
                        t.line = out.size();
                        t.column = p;
                        t.length = q - p + 1;
                        t.type = TokenType::String;
                        out.push_back(t);
                        p = q + 1;
                    } else if (isNumberStart(sv[p])) {
                        size_t q = p;
                        while (q < sv.size() && (std::isdigit((unsigned char)sv[q]) || sv[q] == '.' || sv[q] == '-' || sv[q] == '+')) ++q;
                        Token t;
                        t.line = out.size();
                        t.column = p;
                        t.length = q - p;
                        t.type = TokenType::Number;
                        out.push_back(t);
                        p = q;
                    } else {
                        ++p;
                    }
                }
            } else if (trimmed != std::string_view::npos && sv[trimmed] == '#') {
                Token t;
                t.line = out.size();
                t.column = trimmed;
                t.length = sv.size() - trimmed;
                t.type = TokenType::Keyword;
                out.push_back(t);
            }
            out.push_back(Token{});
            pos = end + 1;
        }
        if (!out.empty() && out.back().type == TokenType::Text && out.back().length == 0) out.pop_back();
        return out;
    }

    const auto ls = lines(doc);
    bool inBlockComment = doc.language == Language::Cpp && doc.content.rfind("/*") != std::string::npos &&
                          doc.content.find("*/", doc.content.rfind("/*")) == std::string::npos;
    for (size_t li = 0; li < ls.size(); ++li) {
        const std::string& line = ls[li];
        size_t i = 0;
        while (i < line.size()) {
            if (inBlockComment) {
                const size_t close = line.find("*/", i);
                Token t{li, i, (close == std::string::npos ? line.size() : close + 2) - i, TokenType::Comment};
                out.push_back(t);
                i = close == std::string::npos ? line.size() : close + 2;
                if (close != std::string::npos) inBlockComment = false;
                continue;
            }
            if (doc.language == Language::Cpp && line.compare(i, 2, "//") == 0) {
                out.push_back({li, i, line.size() - i, TokenType::Comment});
                break;
            }
            if (doc.language == Language::Python && line[i] == '#') {
                out.push_back({li, i, line.size() - i, TokenType::Comment});
                break;
            }
            if (doc.language == Language::Cpp && line.compare(i, 2, "/*") == 0) {
                const size_t close = line.find("*/", i);
                out.push_back({li, i, (close == std::string::npos ? line.size() : close + 2) - i, TokenType::Comment});
                i = close == std::string::npos ? line.size() : close + 2;
                if (close == std::string::npos) inBlockComment = true;
                continue;
            }
            if (line[i] == '"' || line[i] == '\'') {
                char quote = line[i];
                size_t j = i + 1;
                while (j < line.size() && !(line[j] == quote && line[j - 1] != '\\')) ++j;
                out.push_back({li, i, std::min(j + 1, line.size()) - i, TokenType::String});
                i = j + 1;
                continue;
            }
            if (isNumberStart(line[i])) {
                size_t j = i;
                while (j < line.size() && (std::isdigit((unsigned char)line[j]) || line[j] == '.')) ++j;
                out.push_back({li, i, j - i, TokenType::Number});
                i = j;
                continue;
            }
            if (isalpha((unsigned char)line[i]) || line[i] == '_') {
                size_t j = i;
                while (j < line.size() && isIdentifierChar(line[j])) ++j;
                const std::string word = line.substr(i, j - i);
                if (isKeyword(doc.language, word))
                    out.push_back({li, i, j - i, TokenType::Keyword});
                else
                    out.push_back({li, i, j - i, TokenType::Text});
                i = j;
                continue;
            }
            if (strchr("{}[]()<>=+-*/%!&|;:,.", line[i]) != nullptr)
                out.push_back({li, i, 1, TokenType::Punctuation});
            ++i;
        }
    }
    return out;
}

std::vector<std::string> TextEditorModule::lines(const Document& doc) const {
    std::vector<std::string> result;
    size_t start = 0;
    while (true) {
        const size_t nl = doc.content.find('\n', start);
        if (nl == std::string::npos) {
            result.emplace_back(doc.content.substr(start));
            break;
        }
        result.emplace_back(doc.content.substr(start, nl - start));
        start = nl + 1;
    }
    return result;
}

std::vector<SearchMatch> TextEditorModule::find(const Document& doc, const std::string& needle,
                                                bool caseSensitive) const {
    std::vector<SearchMatch> matches;
    if (needle.empty()) return matches;
    std::string hay = doc.content;
    std::string nd = needle;
    if (!caseSensitive) {
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::transform(nd.begin(), nd.end(), nd.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    }
    size_t pos = hay.find(nd);
    while (pos != std::string::npos) {
        matches.push_back({pos, needle.size()});
        pos = hay.find(nd, pos + nd.size());
    }
    return matches;
}

size_t TextEditorModule::replaceAll(const std::string& id, const std::string& needle,
                                    const std::string& replacement, bool caseSensitive) {
    Document* d = findDoc(id);
    if (!d || d->readOnly) return 0;
    const auto matches = find(*d, needle, caseSensitive);
    if (matches.empty()) return 0;
    std::string updated;
    updated.reserve(d->content.size());
    size_t last = 0;
    for (const SearchMatch m : matches) {
        updated.append(d->content, last, m.start - last);
        updated.append(replacement);
        last = m.start + m.length;
    }
    updated.append(d->content, last, d->content.size() - last);
    d->content = std::move(updated);
    d->dirty = true;
    return matches.size();
}

std::vector<size_t> TextEditorModule::matchingBraceLines(const Document& doc) const {
    std::vector<size_t> stack;
    std::vector<size_t> matched;
    const auto ls = lines(doc);
    for (size_t li = 0; li < ls.size(); ++li) {
        for (const char c : ls[li]) {
            if (c == '{') stack.push_back(li);
            if (c == '}' && !stack.empty()) {
                matched.push_back(stack.back());
                stack.pop_back();
            }
        }
    }
    return matched;
}

}  // namespace me::text_editor
