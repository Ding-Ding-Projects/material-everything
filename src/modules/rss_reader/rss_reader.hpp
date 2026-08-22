#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace me::rss {

struct Feed {
    std::string id;
    std::string title;
    std::string url;
    std::string folder;          // empty means "All feeds" root
    std::string description;
    std::chrono::system_clock::time_point last_refreshed{};
};

struct Article {
    std::string id;
    std::string feed_id;
    std::string title;
    std::string link;
    std::string summary;         // short preview text for the article list
    std::string content;         // longer text for the preview pane
    std::chrono::system_clock::time_point published{};
    bool read = false;
    bool starred = false;
};

// Pluggable network boundary so the module stays testable offline.
class FeedFetcher {
public:
    virtual ~FeedFetcher() = default;
    // Returns the raw XML body of the feed URL, or empty on failure.
    virtual std::string fetch(const std::string& url) = 0;
};

class RssReader {
public:
    RssReader();
    ~RssReader();
    RssReader(RssReader&&) noexcept;
    RssReader& operator=(RssReader&&) noexcept;

    void set_fetcher(std::shared_ptr<FeedFetcher> fetcher);

    // ---- Feeds & folders -------------------------------------------------
    std::string add_feed(const std::string& url, const std::string& title,
                         const std::string& folder = "");
    bool remove_feed(const std::string& feed_id);
    bool rename_feed(const std::string& feed_id, const std::string& title);
    bool move_feed(const std::string& feed_id, const std::string& folder);
    const std::vector<Feed>& feeds() const;
    std::vector<Feed> feeds_in_folder(const std::string& folder) const;
    const Feed* find_feed(const std::string& feed_id) const;
    std::vector<std::string> folders() const;

    // ---- Reading ----------------------------------------------------------
    const std::vector<Article>& articles_for(const std::string& feed_id) const;
    int unread_count(const std::string& feed_id) const;
    int total_unread() const;
    const Article* select_article(const std::string& article_id);  // marks read
    void mark_read(const std::string& article_id, bool read);
    void mark_all_read(const std::string& feed_id);
    void toggle_star(const std::string& article_id);
    std::vector<const Article*> starred_articles() const;

    // ---- Refresh ----------------------------------------------------------
    // Parses raw XML (RSS 2.0 or Atom) into the given feed's articles.
    // Returns number of new articles merged, or -1 if the feed is unknown.
    int ingest(const std::string& feed_id, const std::string& xml);
    // Fetches every due feed through the configured fetcher.
    // Returns number of feeds successfully refreshed.
    int refresh_due(std::chrono::system_clock::time_point now =
                        std::chrono::system_clock::now());
    void set_refresh_interval(std::chrono::seconds interval);
    std::chrono::seconds refresh_interval() const;
    bool is_due(const std::string& feed_id,
                std::chrono::system_clock::time_point now =
                    std::chrono::system_clock::now()) const;

    // ---- OPML import / export ---------------------------------------------
    // Imports outline entries as feeds; returns number imported.
    int import_opml(const std::string& opml_xml);
    // Exports all feeds as an OPML 2.0 document.
    std::string export_opml() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace me::rss
