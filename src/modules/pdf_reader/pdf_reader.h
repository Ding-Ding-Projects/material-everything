#pragma once

#include <memory>
#include <string>
#include <vector>

namespace material_everything::pdf {

struct OutlineItem {
    std::string title;
    int page = -1;
    std::vector<OutlineItem> children;
};

struct SearchResult {
    int page = -1;
    int match_start = -1;
    int match_length = 0;
    std::string context;
};

struct Bookmark {
    std::string label;
    int page = -1;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    virtual bool open(const std::string& path) = 0;
    virtual int page_count() const = 0;
    virtual std::string page_text(int page) const = 0;
    virtual std::vector<OutlineItem> outline() const = 0;
};

class PdfReader final {
public:
    explicit PdfReader(std::unique_ptr<RenderBackend> backend);
    bool open(const std::string& path);
    bool next_page();
    bool previous_page();
    bool go_to_page(int page);
    int current_page() const { return current_page_; }
    int page_count() const { return page_count_; }
    void set_zoom(double factor);
    double zoom() const { return zoom_; }
    void set_fit_to_width(bool enabled);
    bool fit_to_width() const { return fit_width_; }
    const std::vector<OutlineItem>& outline() const { return outline_; }
    std::vector<SearchResult> search(const std::string& query) const;
    void add_bookmark(const std::string& label);
    const std::vector<Bookmark>& bookmarks() const { return bookmarks_; }

private:
    std::unique_ptr<RenderBackend> backend_;
    std::vector<std::string> page_cache_;
    std::vector<OutlineItem> outline_;
    std::vector<Bookmark> bookmarks_;
    int current_page_ = 0;
    int page_count_ = 0;
    double zoom_ = 1.0;
    bool fit_width_ = false;
};

}  // namespace material_everything::pdf
