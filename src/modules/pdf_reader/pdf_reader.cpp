#include "pdf_reader.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace material_everything::pdf {
namespace {

std::string lower_copy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}  // namespace

PdfReader::PdfReader(std::unique_ptr<RenderBackend> backend)
    : backend_(std::move(backend)) {}

bool PdfReader::open(const std::string& path) {
    if (!backend_ || !backend_->open(path)) return false;
    page_count_ = backend_->page_count();
    page_cache_.clear();
    for (int i = 0; i < page_count_; ++i) page_cache_.push_back(backend_->page_text(i));
    outline_ = backend_->outline();
    current_page_ = page_count_ ? 0 : -1;
    return page_count_ > 0;
}

bool PdfReader::next_page() { return go_to_page(current_page_ + 1); }
bool PdfReader::previous_page() { return go_to_page(current_page_ - 1); }

bool PdfReader::go_to_page(int page) {
    if (page < 0 || page >= page_count_) return false;
    current_page_ = page;
    return true;
}

void PdfReader::set_zoom(double factor) {
    zoom_ = std::clamp(factor, 0.1, 8.0);
    if (zoom_ != 1.0) fit_width_ = false;
}

void PdfReader::set_fit_to_width(bool enabled) { fit_width_ = enabled; }

std::vector<SearchResult> PdfReader::search(const std::string& query) const {
    std::vector<SearchResult> results;
    if (query.empty()) return results;
    const std::string needle = lower_copy(query);
    for (int page = 0; page < page_count_; ++page) {
        const std::string haystack = lower_copy(page_cache_[static_cast<size_t>(page)]);
        size_t at = haystack.find(needle);
        while (at != std::string::npos) {
            const size_t start = at > 24U ? at - 24U : 0U;
            const size_t end = std::min(haystack.size(), at + needle.size() + 24U);
            results.push_back({page, static_cast<int>(at), static_cast<int>(needle.size()),
                               haystack.substr(start, end - start)});
            at = haystack.find(needle, at + needle.size());
        }
    }
    return results;
}

void PdfReader::add_bookmark(const std::string& label) {
    bookmarks_.push_back({label.empty() ? "Page " + std::to_string(current_page_ + 1) : label,
                          current_page_});
}

}  // namespace material_everything::pdf
