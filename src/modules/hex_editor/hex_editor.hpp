#pragma once

// Material Everything — Hex Editor module
// Hex/binary file editor: hex + ASCII columns, goto, hex/ASCII search,
// byte editing with undo/redo, file info, checksums (MD5/SHA-1/SHA-256),
// and offset bookmarks. Material Design 3 monospace grid.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace material_everything::hex_editor {

/// A byte-level edit recorded for undo/redo.
struct ByteEdit {
    std::uint64_t offset;
    std::uint8_t oldByte;
    std::uint8_t newByte;
};

/// One saved position in the file.
struct Bookmark {
    std::uint64_t offset;
    std::string label;
};

/// Basic metadata about the loaded file.
struct FileInfo {
    std::string path;
    std::uint64_t size = 0;
    std::string sizeHuman;
};

enum class ChecksumAlgorithm {
    MD5,
    SHA1,
    SHA256,
};

enum class SearchKind {
    HexPattern,
    AsciiString,
};

class HexEditor {
public:
    HexEditor() = default;

    // -- File lifecycle -----------------------------------------------------

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return static_cast<bool>(stream_); }
    const FileInfo& fileInfo() const { return info_; }

    // -- Viewport ------------------------------------------------------------

    /// Number of bytes shown per row in the grid.
    int bytesPerRow() const { return bytesPerRow_; }
    void setBytesPerRow(int n);

    /// First byte offset currently visible at the top of the grid.
    std::uint64_t viewportStart() const { return viewportStart_; }
    void setViewportStart(std::uint64_t offset);

    /// Move the viewport so `offset` is visible; returns true when open.
    bool gotoOffset(std::uint64_t offset);

    // -- Reading / editing ----------------------------------------------------

    /// Read up to maxBytes starting at `offset` from the effective view.
    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t maxBytes) const;

    /// Write one byte at `offset`; records an undo step.
    bool editByte(std::uint64_t offset, std::uint8_t value);

    /// True when any unapplied edits exist.
    bool isDirty() const { return !edits_.empty(); }

    // -- Undo / redo ----------------------------------------------------------

    bool canUndo() const { return undoCursor_ > 0; }
    bool canRedo() const { return undoCursor_ < history_.size(); }
    bool undo();
    bool redo();

    // -- Search -----------------------------------------------------------------

    /// Find next match at or after `from`. Returns start offset or empty.
    std::optional<std::uint64_t> find(SearchKind kind, const std::string& query,
                                      std::uint64_t from) const;

    // -- Bookmarks ---------------------------------------------------------------

    void addBookmark(std::uint64_t offset, const std::string& label);
    bool removeBookmark(std::uint64_t offset);
    const std::vector<Bookmark>& bookmarks() const { return bookmarks_; }

    // -- Checksums ------------------------------------------------------------------

    /// Compute a checksum over the full effective view. Returns lowercase hex.
    std::string checksum(ChecksumAlgorithm algo) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    // Pimpl-free storage kept simple for this module:
    mutable std::optional<std::FILE*> stream_;
    std::vector<ByteEdit> edits_;       // pending edits keyed by offset order
    std::vector<std::size_t> history_;  // indices into edits_, undo cursor
    std::size_t undoCursor_ = 0;

    std::vector<Bookmark> bookmarks_;
    FileInfo info_;
    int bytesPerRow_ = 16;
    std::uint64_t viewportStart_ = 0;
};

}  // namespace material_everything::hex_editor
