#pragma once
// Material Everything — HTTP/API client module.
// A self-contained REST client surface: request builder, authentication,
// response inspection, and a saved-request collection.

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace me::api_client {

enum class HttpMethod { Get, Post, Put, Delete, Patch };

enum class AuthType { None, Bearer, Basic, ApiKey };

struct Header {
    std::string key;
    std::string value;
};

enum class BodyMode { Raw, Json, FormData };

struct AuthConfig {
    AuthType type = AuthType::None;
    std::string bearer_token;
    std::string basic_username;
    std::string basic_password;
    std::string api_key_name;   // e.g. "X-Api-Key"
    std::string api_key_value;
};

struct SavedRequest {
    std::string id;
    std::string name;
    HttpMethod method = HttpMethod::Get;
    std::string url;
    std::vector<Header> headers;
    BodyMode body_mode = BodyMode::Raw;
    std::string body;
    AuthConfig auth;
};

struct HttpResponse {
    int status_code = 0;
    std::string status_text;
    long elapsed_ms = 0;
    size_t body_size = 0;
    std::vector<Header> headers;
    std::string raw_body;
};

// Returns "GET", "POST", etc.
std::string to_string(HttpMethod m);

// Pretty-print JSON or XML if the content looks like either; otherwise
// return the body unchanged. Bounded so huge responses stay responsive.
std::string pretty_print(const std::string& body);

class ApiClient {
public:
    // --- Request building -------------------------------------------------
    void set_method(HttpMethod m) { method_ = m; }
    void set_url(std::string url) { url_ = std::move(url); }
    void set_header(std::string key, std::string value);
    void remove_header(const std::string& key);
    void set_body(BodyMode mode, std::string body);
    void set_auth(AuthConfig auth);

    // --- Execution --------------------------------------------------------
    // Performs the HTTP request synchronously and returns the response.
    // Returns nullopt on transport failure (DNS, TLS, timeout, refused).
    std::optional<HttpResponse> execute();

    // --- Saved requests ---------------------------------------------------
    void save_request(const std::string& name);
    bool load_request(const std::string& id);
    void delete_request(const std::string& id);
    const std::vector<SavedRequest>& saved_requests() const { return saved_; }

private:
    HttpMethod method_ = HttpMethod::Get;
    std::string url_;
    std::vector<Header> headers_;
    BodyMode body_mode_ = BodyMode::Raw;
    std::string body_;
    AuthConfig auth_;
    std::vector<SavedRequest> saved_;
};

}  // namespace me::api_client
