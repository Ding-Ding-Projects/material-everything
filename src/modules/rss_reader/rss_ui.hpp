#pragma once

// Material Design 3 view data for the RSS reader module.

#include <string>
#include <vector>

#include "rss_reader.hpp"

namespace me::rss::ui {

struct NavItem {
    std::string label;          // folder name or feed title
    int unread = 0;
    bool selected = false;
    bool is_folder = false;
};

struct ArticleRow {
    std::string id;
    std::string title;
    std::string preview;
    std::string published_text;
    bool read = false;
    bool starred = false;
};

// Navigation-rail items: "All feeds" + folders + feeds.
std::vector<NavItem> navigation(const RssReader& reader,
                                const std::string& selected_id);

// Article list rows for a feed (or every feed when feed_id is empty).
std::vector<ArticleRow> article_list(const RssReader& reader,
                                     const std::string& feed_id);

// Preview pane content for the currently selected article.
struct PreviewPane {
    std::string title;
    std::string body;
    std::string link;
    bool starred = false;
};
PreviewPane preview_for(const RssReader& reader,
                        const std::string& article_id);

}  // namespace me::rss::ui
