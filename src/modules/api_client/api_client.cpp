#include "api_client.hpp"

#include <algorithm>
#include <sstream>

namespace me::api_client {

namespace {

constexpr size_t kMaxPrettySize = 2 * 1024 * 1024;  // 2 MiB pretty-print cap

bool looks_like_json(const std::string& s) {
    auto t = s;
    t.erase(0, t.find_first_not_of(" \t\r\n"));
    return !t.empty() && (t.front() == '{' || t.front() == '[');
}

bool looks_like_xml(const std::string& s) {
    auto t = s;
    t.erase(0, t.find_first_not_of(" \t\r\n"));
    return !t.empty() && t.front() == '<';
}

// Minimal JSON indenter: tracks strings/escapes, adds newlines + indent.
std::string indent_json(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3 / 2);
    int depth = 0;
    bool in_string = false, escaped = false;
    for (char c : in) {
        if (in_string) {
            out += c;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        switch (c) {
            case '"':
                in_string = true;
                out += c;
                break;
            case '{': case '[':
                out += c;
                out += '\n';
                ++depth;
                out.append(depth * 2, ' ');
                break;
            case '}': case ']':
                out += '\n';
                --depth;
                out.append(std::max(0, depth) * 2, ' ');
                out += c;
                break;
            case ',':
                out += c;
                out += '\n';
                out.append(std::max(0, depth) * 2, ' ');
                break;
            case ':':
                out += ": ";
                break;
            default:
                out += c;
        }
    }
    return out;
}

// Minimal XML indenter for well-formed-ish documents: newline between tags.
std::string indent_xml(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 5 / 4);
    int depth = 0;
    size_t i = 0;
    while (i < in.size()) {
        auto lt = in.find('<', i);
        if (lt == std::string::npos) { out.append(in.substr(i)); break; }
        if (lt > i) {
            auto text = in.substr(i, lt - i);
            if (!text.find_first_not_of(" \t\r\n") != std::string::npos || !text.empty())
                if (!text.empty()) { out += text; }
        }
        auto gt = in.find('>', lt);
        if (gt == std::string::npos) { out.append(in.substr(lt)); break; }
        auto tag = in.substr(lt, gt - lt + 1);
        bool closing = tag.rfind("</", 0) == 0;
        bool self_close = tag.back() == '/';
        bool decl = tag.rfind("<?", 0) == 0 || tag.rfind("<!", 0) == 0;
        if (closing) --depth;
        out += '\n';
        out.append(std::max(0, depth) * 2, ' ');
        out += tag;
        if (!closing && !self_close && !decl) ++depth;
        i = gt + 1;
    }
    return out;
}

}  // namespace

std::string to_string(HttpMethod m) {
    switch (m) {
        case HttpMethod::Get: return "GET";
        case HttpMethod::Post: return "POST";
        case HttpMethod::Put: return "PUT";
        case HttpMethod::Delete: return "DELETE";
        case HttpMethod::Patch: return "PATCH";
    }
    return "GET";
}

std::string pretty_print(const std::string& body) {
    if (body.size() > kMaxPrettySize) return body;
    if (looks_like_json(body)) return indent_json(body);
    if (looks_like_xml(body)) return indent_xml(body);
    return body;
}

void ApiClient::set_header(std::string key, std::string value) {
    for (auto& h : headers_)
        if (h.key == key) { h.value = std::move(value); return; }
    headers_.push_back({std::move(key), std::move(value)});
}

void ApiClient::remove_header(const std::string& key) {
    headers_.erase(
        std::remove_if(headers_.begin(), headers_.end(),
                       [&](const Header& h) { return h.key == key; }),
        headers_.end());
}

void ApiClient::set_body(BodyMode mode, std::string body) {
    body_mode_ = mode;
    body_ = std::move(body);
}

void ApiClient::set_auth(AuthConfig auth) {
    auth_ = std::move(auth);
    // Materialize auth into concrete headers at build time.
    switch (auth_.type) {
        case AuthType::Bearer:
            remove_header("Authorization");
            set_header("Authorization", "Bearer " + auth_.bearer_token);
            break;
        case AuthType::ApiKey:
            if (!auth_.api_key_name.empty())
                set_header(auth_.api_key_name, auth_.api_key_value);
            break;
        default:
            break;
    }
}

std::optional<HttpResponse> ApiClient::execute() {
    // Transport is delegated to the app's shared networking layer at link
    // time. The module owns protocol semantics only; this seam keeps it
    // testable without a live socket.
    //
    // TODO(integration): bind to libcurl session pool owned by the shell.
    // For now this returns no response rather than pretending one arrived.
    (void)this;
    return std::nullopt;
}

void ApiClient::save_request(const std::string& name) {
    SavedRequest r;
    r.id = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    r.name = name;
    r.method = method_;
    r.url = url_;
    r.headers = headers_;
    r.body_mode = body_mode_;
    r.body = body_;
    r.auth = auth_;
    saved_.push_back(std::move(r));
}

bool ApiClient::load_request(const std::string& id) {
    auto it = std::find_if(saved_.begin(), saved_.end(),
                           [&](const SavedRequest& r) { return r.id == id; });
    if (it == saved_.end()) return false;
    method_ = it->method;
    url_ = it->url;
    headers_ = it->headers;
    body_mode_ = it->body_mode;
    body_ = it->body;
    auth_ = it->auth;
    return true;
}

void ApiClient::delete_request(const std::string& id) {
    saved_.erase(std::remove_if(saved_.begin(), saved_.end(),
                                [&](const SavedRequest& r) { return r.id == id; }),
                 saved_.end());
}

}  // namespace me::api_client
