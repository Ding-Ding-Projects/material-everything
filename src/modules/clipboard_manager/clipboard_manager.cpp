#include "clipboard_manager.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

namespace material_everything::modules {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string lowercase(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool matches_search(const ClipboardEntry& entry, const std::string& query) {
    if (query.empty()) return true;
    if (!entry.text_preview.empty()) {
        if (lowercase(entry.text_preview).find(lowercase(query)) != std::string::npos) return true;
    }
    for (const auto& path : entry.file_paths) {
        if (lowercase(path).find(lowercase(query)) != std::string::npos) return true;
    }
    return false;
}

void trim_to_limit(std::vector<ClipboardEntry>& entries, std::size_t limit) {
    while (entries.size() > limit) {
        auto victim = std::max_element(entries.begin(), entries.end(),
            [](const ClipboardEntry& a, const ClipboardEntry& b) {
                if (a.pinned != b.pinned) return !a.pinned;   // pinned survives
                return a.timestamp_ms < b.timestamp_ms;
            });
        entries.erase(victim);
    }
}

}  // namespace

ClipboardManagerModule::ClipboardManagerModule(std::size_t history_limit)
    : history_limit_(history_limit == 0 ? 1 : history_limit) {}

void ClipboardManagerModule::set_history_limit(std::size_t limit) {
    history_limit_ = limit == 0 ? 1 : limit;
    trim_to_limit(entries_, history_limit_);
}

ClipboardEntry& ClipboardManagerModule::add_entry(ClipboardEntry entry) {
    entry.id.reserve(24);
    entry.id += std::to_string(entry.timestamp_ms);
    if (entry.id.size() < 8) entry.id.insert(0, 8 - entry.id.size(), '0');
    entry.id += "-" + std::to_string(entries_.size());
    if (entry.timestamp_ms == 0) entry.timestamp_ms = now_ms();
    entries_.push_back(std::move(entry));
    std::stable_sort(entries_.begin(), entries_.end(), [](const ClipboardEntry& a, const ClipboardEntry& b) {
        if (a.pinned != b.pinned) return a.pinned > b.pinned;
        return a.timestamp_ms > b.timestamp_ms;
    });
    trim_to_limit(entries_, history_limit_);
    return entries_.front();
}

std::vector<ClipboardEntry> ClipboardManagerModule::entries(const std::string& search_query) const {
    std::vector<ClipboardEntry> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_) {
        if (matches_search(e, search_query)) result.push_back(e);
    }
    return result;
}

const ClipboardEntry* ClipboardManagerModule::find(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&](const ClipboardEntry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

void ClipboardManagerModule::toggle_pin(const std::string& id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&](const ClipboardEntry& e) { return e.id == id; });
    if (it != entries_.end()) it->pinned = !it->pinned;
}

void ClipboardManagerModule::remove(const std::string& id) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&](const ClipboardEntry& e) { return e.id == id; });
    if (it != entries_.end()) entries_.erase(it);
}

}  // namespace material_everything::modules
