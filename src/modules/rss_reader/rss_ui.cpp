#include "rss_ui.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace me::rss::ui {

namespace {

std::string time_text(const std::chrono::system_clock::time_point& tp) {
    if (tp == std::chrono::system_clock::time_point{}) return "";
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%b %d");
    return out.str();
}

}  // namespace

std::vector<NavItem> navigation(const RssReader& reader,
                                const std::string& selected_id) {
    std::vector<NavItem> items;

    NavItem all;
    all.label = "All feeds";
    all.unread = reader.total_unread();
    all.is_folder = true;
    all.selected = selected_id.empty();
    items.push_back(std::move(all));

    for (const auto& folder : reader.folders()) {
        NavItem fi;
        fi.label = folder;
        fi.is_folder = true;
        fi.selected = selected_id == folder;
        int unread = 0;
        for (const auto& f : reader.feeds_in_folder(folder))
            unread += reader.unread_count(f.id);
        fi.unread = unread;
        items.push_back(std::move(fi));
    }

    for (const auto& f : reader.feeds()) {
        NavItem ni;
        ni.label = f.title;
        ni.unread = reader.unread_count(f.id);
        ni.is_folder = false;
        ni.selected = selected_id == f.id;
        items.push_back(std::move(ni));
    }
    return items;
}

std::vector<ArticleRow> article_list(const RssReader& reader,
                                     const std::string& feed_id) {
    std::vector<ArticleRow> rows;
    auto push = [&](const Article& a) {
        ArticleRow r;
        r.id = a.id;
        r.title = a.title;
        const std::size_t cap = 120;
        r.preview = a.summary.empty()
            ? a.content.substr(0, a.content.size() < cap ? a.content.size() : cap)
            : a.summary;
        r.published_text = time_text(a.published);
        r.read = a.read;
        r.starred = a.starred;
        rows.push_back(std::move(r));
    };
    if (feed_id.empty()) {
        for (const auto& f : reader.feeds())
            for (const auto& a : reader.articles_for(f.id)) push(a);
    } else {
        for (const auto& a : reader.articles_for(feed_id)) push(a);
    }
    return rows;
}

PreviewPane preview_for(const RssReader& reader,
                        const std::string& article_id) {
    PreviewPane p;
    for (const auto& f : reader.feeds())
        for (const auto& a : reader.articles_for(f.id))
            if (a.id == article_id) {
                p.title = a.title;
                p.body = a.content.empty() ? a.summary : a.content;
                p.link = a.link;
                p.starred = a.starred;
                return p;
            }
    return p;
}

}  // namespace me::rss::ui
