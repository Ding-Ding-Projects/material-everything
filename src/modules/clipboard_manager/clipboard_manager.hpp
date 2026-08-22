#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace material_everything::modules {

enum class ClipboardEntryKind : std::uint8_t {
    Text,
    Image,
    Files,
};

struct ClipboardEntry {
    std::string id;
    ClipboardEntryKind kind;
    std::string text_preview;
    std::vector<std::uint8_t> image_bytes;
    int image_width = 0;
    int image_height = 0;
    std::vector<std::string> file_paths;
    bool pinned = false;
    std::int64_t timestamp_ms = 0;
};

class ClipboardManagerModule final {
public:
    explicit ClipboardManagerModule(std::size_t history_limit = 200);

    void set_history_limit(std::size_t limit);
    std::size_t history_limit() const { return history_limit_; }

    // Called by the platform clipboard bridge whenever a new item is copied.
    ClipboardEntry& add_entry(ClipboardEntry entry);

    std::vector<ClipboardEntry> entries(const std::string& search_query = {}) const;
    const ClipboardEntry* find(const std::string& id) const;
    void toggle_pin(const std::string& id);
    void remove(const std::string& id);

private:
    std::size_t history_limit_;
    std::vector<ClipboardEntry> entries_;
};

}  // namespace material_everything::modules
