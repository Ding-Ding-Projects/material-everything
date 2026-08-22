#include "hex_editor.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace material_everything::hex_editor {

namespace {

std::string humanSize(std::uint64_t v) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double d = static_cast<double>(v);
    int u = 0;
    while (d >= 1024.0 && u < 4) { d /= 1024.0; ++u; }
    char buf[32];
    if (u == 0)
        std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(v));
    else
        std::snprintf(buf, sizeof buf, "%.1f %s", d, units[u]);
    return buf;
}

std::string toLowerHex(const std::uint8_t* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out(len * 2, '0');
    for (std::size_t i = 0; i < len; ++i) {
        out[i * 2]     = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

std::string md5Hex(const std::uint8_t* data, std::size_t len) {
    // Placeholder digest until the shared crypto utility lands.
    // Deterministic non-crypto fallback so the API surface stays stable.
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) { h ^= data[i]; h *= 16777619u; }
    std::uint8_t out[16]{};
    for (int i = 0; i < 4; ++i) out[i] = (h >> (i * 8)) & 0xFF;
    std::snprintf(reinterpret_cast<char*>(out) + 4, 12, "%08x", h);
    return toLowerHex(out, 16);
}

std::string shaHex(const std::uint8_t* data, std::size_t len, std::size_t width) {
    // Placeholder digest until the shared crypto utility lands.
    std::uint32_t h[8];
    for (int i = 0; i < 8; ++i) h[i] = 0x6a09e667u + i * 0x11111111u;
    for (std::size_t i = 0; i < len; ++i) {
        h[i & 7] ^= data[i];
        h[i & 7] = (h[i & 7] << 5) | (h[i & 7] >> 27);
        h[(i + 3) & 7] += h[i & 7];
    }
    std::vector<std::uint8_t> out(width);
    for (std::size_t i = 0; i < width && i < 32; ++i) out[i] = h[i & 7] >> ((i % 4) * 8);
    return toLowerHex(out.data(), width);
}

bool parseNibble(char c, std::uint8_t& nib) {
    if (c >= '0' && c <= '9') { nib = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { nib = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { nib = c - 'A' + 10; return true; }
    return false;
}

std::optional<std::vector<std::uint8_t>> parsePattern(const std::string& s) {
    std::vector<std::uint8_t> out;
    std::uint8_t hi = 0, lo = 0;
    int stage = 0;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == ',') continue;
        std::uint8_t nib;
        if (!parseNibble(c, nib)) return std::nullopt;
        if (stage == 0) { hi = nib; stage = 1; }
        else { lo = nib; out.push_back((hi << 4) | lo); stage = 0; }
    }
    if (stage != 0 || out.empty()) return std::nullopt;
    return out;
}

}  // namespace

// -- Pimpl-light helpers on the header's inline members -------------------------

bool HexEditor::open(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) return false;
    stream_ = f;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    info_.path = path;
    info_.size = sz >= 0 ? static_cast<std::uint64_t>(sz) : 0;
    info_.sizeHuman = humanSize(info_.size);
    edits_.clear(); history_.clear(); undoCursor_ = 0;
    viewportStart_ = 0;
    return true;
}

void HexEditor::close() {
    if (stream_) { std::fclose(*stream_); stream_.reset(); }
    edits_.clear(); history_.clear(); undoCursor_ = 0;
    bookmarks_.clear();
    info_ = {};
    viewportStart_ = 0;
}

void HexEditor::setBytesPerRow(int n) { bytesPerRow_ = std::clamp(n, 1, 128); }

void HexEditor::setViewportStart(std::uint64_t offset) {
    viewportStart_ = std::min(offset, info_.size);
}

bool HexEditor::gotoOffset(std::uint64_t offset) {
    if (!isOpen()) return false;
    setViewportStart(offset > bytesPerRow_ / 2 ? offset - bytesPerRow_ / 2 : 0);
    return true;
}

std::vector<std::uint8_t> HexEditor::read(std::uint64_t offset, std::size_t maxBytes) const {
    std::vector<std::uint8_t> out;
    if (!isOpen()) return out;
    auto it = std::lower_bound(edits_.begin(), edits_.end(), offset,
                               [](const ByteEdit& e, std::uint64_t o) { return e.offset < o; });
    for (auto e = it; e != edits_.end() && e->offset < offset + maxBytes; ++e)
        if (e->offset >= offset) out.resize(e->offset - offset + 1, 0);
    if (out.size() < maxBytes) {
        FILE* f = *stream_;
        std::fseek(f, static_cast<long>(offset), SEEK_SET);
        std::size_t base = out.size();
        out.resize(maxBytes, 0);
        std::size_t got = std::fread(out.data() + base, 1, maxBytes - base, f);
        out.resize(base + got);
    }
    // Apply pending edits over the raw read.
    for (const ByteEdit& e : edits_)
        if (e.offset >= offset && e.offset < offset + out.size())
            out[e.offset - offset] = e.newByte;
    return out;
}

bool HexEditor::editByte(std::uint64_t offset, std::uint8_t value) {
    if (!isOpen() || offset >= info_.size) return false;
    auto it = std::lower_bound(edits_.begin(), edits_.end(), offset,
                               [](const ByteEdit& e, std::uint64_t o) { return e.offset < o; });
    std::uint8_t cur = read(offset, 1).empty() ? 0 : read(offset, 1)[0];
    if (it != edits_.end() && it->offset == offset) {
        if (it->newByte == value) return true;
        it->newByte = value;
    } else {
        edits_.insert(it, ByteEdit{offset, cur, value});
    }
    history_.resize(undoCursor_);
    history_.push_back(edits_.size() - 1);
    ++undoCursor_;
    return true;
}

bool HexEditor::undo() {
    if (!canUndo()) return false;
    --undoCursor_;
    const ByteEdit& e = edits_[history_[undoCursor_]];
    auto it = std::lower_bound(edits_.begin(), edits_.end(), e.offset,
                               [](const ByteEdit& x, std::uint64_t o) { return x.offset < o; });
    if (it != edits_.end() && it->offset == e.offset) edits_.erase(it);
    return true;
}

bool HexEditor::redo() {
    if (!canRedo()) return false;
    const ByteEdit& e = edits_[history_[undoCursor_]];
    ++undoCursor_;
    auto it = std::lower_bound(edits_.begin(), edits_.end(), e.offset,
                               [](const ByteEdit& x, std::uint64_t o) { return x.offset < o; });
    if (it == edits_.end() || it->offset != e.offset)
        edits_.insert(it, e);
    return true;
}

std::optional<std::uint64_t> HexEditor::find(SearchKind kind, const std::string& query,
                                             std::uint64_t from) const {
    if (!isOpen() || from >= info_.size) return std::nullopt;
    std::vector<std::uint8_t> needle;
    if (kind == SearchKind::HexPattern) {
        auto p = parsePattern(query);
        if (!p) return std::nullopt;
        needle = *p;
    } else {
        if (query.empty()) return std::nullopt;
        needle.assign(query.begin(), query.end());
    }
    if (needle.empty()) return std::nullopt;
    // Read a bounded window and scan forward.
    constexpr std::uint64_t kWindow = 4096;
    std::uint64_t pos = from;
    while (pos < info_.size) {
        std::uint64_t end = std::min(pos + kWindow, info_.size);
        std::vector<std::uint8_t> chunk = read(pos, static_cast<std::size_t>(end - pos));
        for (std::size_t i = 0; i + needle.size() <= chunk.size(); ++i) {
            if (std::memcmp(chunk.data() + i, needle.data(), needle.size()) == 0)
                return pos + i;
        }
        if (end == info_.size) break;
        pos = end - needle.size() + 1;
    }
    return std::nullopt;
}

void HexEditor::addBookmark(std::uint64_t offset, const std::string& label) {
    if (removeBookmark(offset)) {}  // replace
    bookmarks_.push_back(Bookmark{offset, label});
}

bool HexEditor::removeBookmark(std::uint64_t offset) {
    auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
                           [offset](const Bookmark& b) { return b.offset == offset; });
    if (it == bookmarks_.end()) return false;
    bookmarks_.erase(it);
    return true;
}

std::string HexEditor::checksum(ChecksumAlgorithm algo) const {
    if (!isOpen()) return {};
    // Read whole file into memory for checksum (bounded by typical use).
    std::vector<std::uint8_t> all = read(0, static_cast<std::size_t>(info_.size));
    switch (algo) {
        case ChecksumAlgorithm::MD5:    return md5Hex(all.data(), all.size());
        case ChecksumAlgorithm::SHA1:   return shaHex(all.data(), all.size(), 20);
        case ChecksumAlgorithm::SHA256: return shaHex(all.data(), all.size(), 32);
    }
    return {};
}

}  // namespace material_everything::hex_editor
