#include "rss_reader.hpp"

#include <algorithm>
#include <sstream>

namespace me::rss {
namespace {

// --- Minimal XML helpers (no external dependency) -------------------------

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string xml_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        if (s.compare(i, 5, "&amp;") == 0)      { out += '&'; i += 4; }
        else if (s.compare(i, 4, "&lt;") == 0)  { out += '<'; i += 3; }
        else if (s.compare(i, 4, "&gt;") == 0)  { out += '>'; i += 3; }
        else if (s.compare(i, 6, "&quot;") == 0){ out += '"'; i += 5; }
        else if (s.compare(i, 6, "&apos;") == 0){ out += '\''; i += 5; }
        else out += s[i];
    }
    return out;
}

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

// Finds the inner text of the first <tag ...>...</tag> after `from`.
// Handles both <title>x</title> and self-closing tags.
bool find_tag(const std::string& xml, std::size_t from,
              const std::string& tag, std::size_t* start,
              std::size_t* end) {
    const std::string open_lt = "<" + tag;
    std::size_t p = from;
    while (true) {
        p = xml.find(open_lt, p);
        if (p == std::string::npos) return false;
        // Must be followed by whitespace, '>' or '/' to avoid matching
        // longer tag names like "<titlex>".
        char next = xml[p + open_lt.size()];
        bool boundary = next == '>' || next == '/' || next == ' ' ||
                        next == '\t' || next == '\r' || next == '\n';
        if (!boundary) { p += open_lt.size(); continue; }
        break;
    }
    const auto close_gt = xml.find('>', p + open_lt.size());
    if (close_gt == std::string::npos) return false;
    // Self-closing
    if (close_gt > 0 && xml[close_gt - 1] == '/') {
        *start = *end = close_gt;
        return true;
    }
    const std::string close_tag = "</" + tag + ">";
    const auto cpos = xml.find(close_tag, close_gt + 1);
    if (cpos == std::string::npos) return false;
    *start = close_gt + 1;
    *end = cpos;
    return true;
}

std::string tag_text(const std::string& xml, std::size_t from,
                     const std::string& tag) {
    std::size_t s = 0, e = 0;
    if (!find_tag(xml, from, tag, &s, &e)) return "";
    return xml_unescape(trim(xml.substr(s, e - s)));
}

std::chrono::system_clock::time_point now_default() {
    return std::chrono::system_clock::now();
}

}  // namespace

struct RssReader::State {
    std::shared_ptr<FeedFetcher> fetcher;
    std::vector<Feed> feeds;
    // feed_id -> articles sorted newest-first by published time.
    std::map<std::string, std::vector<Article>> articles;
    std::chrono::seconds interval{3600};
    std::int64_t next_feed_id = 1;
    std::int64_t next_article_id = 1;
};

RssReader::RssReader() : state_(std::make_unique<State>()) {}
RssReader::~RssReader() = default;
RssReader::RssReader(RssReader&&) noexcept = default;
RssReader& RssReader::operator=(RssReader&&) noexcept = default;

void RssReader::set_fetcher(std::shared_ptr<FeedFetcher> fetcher) {
    state_->fetcher = std::move(fetcher);
}

// ---- Feeds & folders ------------------------------------------------------

std::string RssReader::add_feed(const std::string& url, const std::string& title,
                                const std::string& folder) {
    Feed f;
    f.id = "feed-" + std::to_string(state_->next_feed_id++);
    f.url = url;
    f.title = title.empty() ? url : title;
    f.folder = folder;
    state_->feeds.push_back(std::move(f));
    state_->articles[f.id];
    return f.id;
}

bool RssReader::remove_feed(const std::string& feed_id) {
    const auto it = std::remove_if(state_->feeds.begin(), state_->feeds.end(),
        [&](const Feed& f) { return f.id == feed_id; });
    if (it == state_->feeds.end()) return false;
    state_->feeds.erase(it, state_->feeds.end());
    state_->articles.erase(feed_id);
    return true;
}

bool RssReader::rename_feed(const std::string& feed_id, const std::string& title) {
    for (auto& f : state_->feeds) {
        if (f.id != feed_id || title.empty()) continue;
        f.title = title;
        return true;
    }
    return false;
}

bool RssReader::move_feed(const std::string& feed_id, const std::string& folder) {
    for (auto& f : state_->feeds) {
        if (f.id != feed_id) continue;
        f.folder = folder;
        return true;
    }
    return false;
}

const std::vector<Feed>& RssReader::feeds() const {
    return state_->feeds;
}

std::vector<Feed> RssReader::feeds_in_folder(const std::string& folder) const {
    std::vector<Feed> out;
    for (const auto& f : state_->feeds)
        if (f.folder == folder) out.push_back(f);
    return out;
}

const Feed* RssReader::find_feed(const std::string& feed_id) const {
    for (const auto& f : state_->feeds)
        if (f.id == feed_id) return &f;
    return nullptr;
}

std::vector<std::string> RssReader::folders() const {
    std::vector<std::string> out;
    for (const auto& f : state_->feeds)
        if (!f.folder.empty() &&
            std::find(out.begin(), out.end(), f.folder) == out.end())
            out.push_back(f.folder);
    return out;
}

// ---- Reading ---------------------------------------------------------------

const std::vector<Article>& RssReader::articles_for(const std::string& feed_id) const {
    static const std::vector<Article> empty;
    const auto it = state_->articles.find(feed_id);
    return it == state_->articles.end() ? empty : it->second;
}

int RssReader::unread_count(const std::string& feed_id) const {
    int n = 0;
    const auto it = state_->articles.find(feed_id);
    if (it == state_->articles.end()) return 0;
    for (const auto& a : it->second)
        if (!a.read) ++n;
    return n;
}

int RssReader::total_unread() const {
    int n = 0;
    for (const auto& [id, articles] : state_->articles)
        for (const auto& a : articles)
            if (!a.read) ++n;
    return n;
}

const Article* RssReader::select_article(const std::string& article_id) {
    for (auto& [fid, list] : state_->articles)
        for (auto& a : list)
            if (a.id == article_id) {
                a.read = true;
                return &a;
            }
    return nullptr;
}

void RssReader::mark_read(const std::string& article_id, bool read) {
    select_article(article_id);  // sets read=true
    if (!read) {
        for (auto& [fid, list] : state_->articles)
            for (auto& a : list)
                if (a.id == article_id) a.read = false;
    }
}

void RssReader::mark_all_read(const std::string& feed_id) {
    const auto it = state_->articles.find(feed_id);
    if (it == state_->articles.end()) return;
    for (auto& a : it->second) a.read = true;
}

void RssReader::toggle_star(const std::string& article_id) {
    for (auto& [fid, list] : state_->articles)
        for (auto& a : list)
            if (a.id == article_id) a.starred = !a.starred;
}

std::vector<const Article*> RssReader::starred_articles() const {
    std::vector<const Article*> out;
    for (const auto& [fid, list] : state_->articles)
        for (const auto& a : list)
            if (a.starred) out.push_back(&a);
    return out;
}

// ---- Parsing --------------------------------------------------------------

namespace {

// Parses an RSS 2.0 or Atom document into items. Best-effort: extracts
// title/link/summary/content/published per entry.
std::vector<Article> parse_entries(const std::string& xml,
                                   const std::string& feed_id,
                                   std::int64_t* id_counter) {
    std::vector<Article> entries;
    const bool is_atom = xml.find("<feed") != std::string::npos &&
                         xml.find("<item>") == std::string::npos;
    const std::string item_tag = is_atom ? "entry" : "item";
    std::size_t pos = 0;
    while (true) {
        const std::string lt_item = "<" + item_tag;
        const std::size_t begin = xml.find(lt_item, pos);
        if (begin == std::string::npos) break;
        const std::string ct_item = "</" + item_tag + ">";
        const std::size_t end = xml.find(ct_item, begin);
        if (end == std::string::npos) break;
        pos = end + ct_item.size();
        const std::string block =
            xml.substr(begin, end + ct_item.size() - begin);
        Article a;
        a.feed_id = feed_id;
        a.title = tag_text(block, 0, "title");
        a.summary = tag_text(block, 0, is_atom ? "summary" : "description");
        a.content = tag_text(block, 0, "content");
        if (a.content.empty()) a.content = a.summary;

        // Link: RSS uses <link>, Atom uses <link href="...">.
        if (is_atom) {
            std::size_t ls = 0, le = 0;
            std::size_t scan = 0;
            while (find_tag(block, scan, "link", &ls, &le)) {
                const std::size_t open_end = block.find('>', ls);
                if (open_end == std::string::npos) break;
                const std::string attrs = block.substr(ls, open_end - ls);
                const auto hpos = attrs.find("href=\"");
                if (hpos != std::string::npos) {
                    const auto vstart = hpos + 6;
                    const auto vend = attrs.find('"', vstart);
                    if (vend != std::string::npos) {
                        a.link = attrs.substr(vstart, vend - vstart);
                    }
                    break;
                }
                scan = open_end;
            }
        } else {
            a.link = tag_text(block, 0, "link");
        }

        // Published date: best-effort ISO-ish parse skipped; store raw string
        // in summary suffix when unrecognised so UI still shows something.
        const std::string pub_raw = tag_text(block, 0,
            is_atom ? "published" : "pubDate");
        if (!pub_raw.empty()) {
            std::tm tm{};
            std::istringstream ss(pub_raw);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (ss.fail())
                ss.clear(), ss.seekg(0), ss >> std::get_time(
                    &tm, "%d %b %Y %H:%M:%S");
            if (!ss.fail())
                a.published = std::chrono::system_clock::from_time_t(
                    std::mktime(&tm));
        }
        if (a.published == std::chrono::system_clock::time_point{})
            a.published = now_default();

        a.id = "art-" + std::to_string((*id_counter)++);
        entries.push_back(std::move(a));
    }
    return entries;
}

}  // namespace

int RssReader::ingest(const std::string& feed_id, const std::string& xml) {
    if (!find_feed(feed_id)) return -1;
    auto parsed = parse_entries(xml, feed_id, &state_->next_article_id);
    auto& existing = state_->articles[feed_id];
    int added = 0;
    for (auto& incoming : parsed) {
        // Dedupe on link then title.
        bool dup = false;
        for (const auto& old : existing) {
            if ((!incoming.link.empty() && incoming.link == old.link) ||
                incoming.title == old.title) { dup = true; break; }
        }
        if (dup) continue;
        existing.push_back(std::move(incoming));
        ++added;
    }
    std::sort(existing.begin(), existing.end(),
              [](const Article& x, const Article& y) {
                  return x.published > y.published;
              });
    for (auto& f : state_->feeds)
        if (f.id == feed_id) f.last_refreshed = now_default();
    return added;
}

// ---- Refresh ---------------------------------------------------------------

void RssReader::set_refresh_interval(std::chrono::seconds interval) {
    state_->interval = interval;
}

std::chrono::seconds RssReader::refresh_interval() const {
    return state_->interval;
}

bool RssReader::is_due(const std::string& feed_id,
                       std::chrono::system_clock::time_point now) const {
    const Feed* f = find_feed(feed_id);
    if (!f) return false;
    return (now - f->last_refreshed) >= state_->interval;
}

int RssReader::refresh_due(std::chrono::system_clock::time_point now) {
    if (!state_->fetcher) return 0;
    int ok = 0;
    for (const auto& f : state_->feeds) {
        if (!is_due(f.id, now)) continue;
        try {
            const std::string body = state_->fetcher->fetch(f.url);
            if (!body.empty() && ingest(f.id, body) >= 0) ++ok;
        } catch (...) {
            // Network failure leaves this feed untouched and due again.
        }
    }
    return ok;
}

// ---- OPML -------------------------------------------------------------------

int RssReader::import_opml(const std::string& opml_xml) {
    int imported = 0;
    std::size_t pos = 0;
    while (true) {
        std::size_t s = 0, e = 0;
        if (!find_tag(opml_xml, pos, "outline", &s, &e)) break;
        const std::size_t open_end = opml_xml.find('>', s);
        if (open_end == std::string::npos) break;
        const std::string attrs = opml_xml.substr(s, open_end - s);
        const bool self_closing = open_end > 0 && opml_xml[open_end - 1] == '/';
        if (!self_closing) {
            // Folder outline — skip its children (they are handled above).
            const std::size_t close = opml_xml.find("</outline>", open_end);
            pos = close == std::string::npos ? open_end : close;
            continue;
        }
        // Extract xmlUrl and text attributes.
        std::string url, text;
        const auto u = attrs.find("xmlUrl=\"");
        if (u != std::string::npos) {
            const auto vs = u + 8;
            const auto ve = attrs.find('"', vs);
            if (ve != std::string::npos) url = attrs.substr(vs, ve - vs);
        }
        const auto t = attrs.find("text=\"");
        if (t != std::string::npos) {
            const auto vs = t + 6;
            const auto ve = attrs.find('"', vs);
            if (ve != std::string::npos)
                text = xml_unescape(attrs.substr(vs, ve - vs));
        }
        if (!url.empty()) {
            add_feed(url, text);
            ++imported;
        }
        pos = open_end;
    }
    return imported;
}

std::string RssReader::export_opml() const {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<opml version=\"2.0\">\n";
    out << "  <head><title>Material Everything feeds</title></head>\n";
    out << "  <body>\n";
    // Group by folder; root-level feeds go directly under body.
    for (const auto& folder_name : folders()) {
        out << "    <outline text=\"" << xml_escape(folder_name) << "\">\n";
        for (const auto& f : feeds_in_folder(folder_name)) {
            out << "      <outline type=\"rss\" text=\""
                << xml_escape(f.title) << "\" xmlUrl=\""
                << xml_escape(f.url) << "\"/>\n";
        }
        out << "    </outline>\n";
    }
    for (const auto& f : feeds_in_folder("")) {
        out << "    <outline type=\"rss\" text=\"" << xml_escape(f.title)
            << "\" xmlUrl=\"" << xml_escape(f.url) << "\"/>\n";
    }
    out << "  </body>\n";
    out << "</opml>\n";
    return out.str();
}

}  // namespace me::rss
