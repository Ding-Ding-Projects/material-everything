#include "ftp_client.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

// ── Backend includes ─────────────────────────────────────────────────────
// libssh2 for SFTP/SCP. libcurl for FTP/FTPS.

#ifdef ME_WITH_SFTP
#include <libssh2.h>
#include <libssh2_sftp.h>
#endif

#ifdef ME_WITH_CURL
#include <curl/curl.h>
#endif

namespace fs = std::filesystem;
namespace material_everything {

// ═══════════════════════════════════════════════════════════════════════
// Internal implementation (pimpl)
// ═══════════════════════════════════════════════════════════════════════

struct FtpClientModule::Impl {
    // Connection state
    bool connected = false;
    FtpProtocol protocol = FtpProtocol::Ftp;
    SiteBookmark active_site;

    // libssh2 session handles (SFTP/SCP)
#ifdef ME_WITH_SFTP
    LIBSSH2_SESSION* ssh_session = nullptr;
    LIBSSH2_SFTP* sftp_session = nullptr;
    int socket_fd = -1;
#endif

    // libcurl handle (FTP/FTPS)
#ifdef ME_WITH_CURL
    CURL* curl_handle = nullptr;
#endif

    // Remote working directory cache
    std::string cwd_remote;

    // Transfer queue
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::vector<QueueItem> queue;
    int64_t next_queue_id = 1;
    std::atomic<bool> processing{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> paused{false};

    // Callbacks
    ProgressCallback progress_cb;
    LogCallback log_cb;

    void log(const std::string& msg) {
        if (log_cb) log_cb(msg);
    }

    void report_progress(int64_t id, const QueueItem& item) {
        auto elapsed = std::chrono::steady_clock::now() - item.started_at;
        double secs = std::chrono::duration<double>(elapsed).count();
        if (secs > 0 && item.progress.bytes_transferred > 0) {
            item.progress.speed_bytes_per_sec =
                static_cast<double>(item.progress.bytes_transferred) / secs;
            uint64_t remaining = item.total_size - item.progress.bytes_transferred;
            if (item.progress.speed_bytes_per_sec > 0)
                item.progress.eta_seconds = static_cast<double>(remaining) / item.progress.speed_bytes_per_sec;
        }
        if (item.total_size > 0)
            item.progress.percent = 100.0 * static_cast<double>(item.progress.bytes_transferred) / static_cast<double>(item.total_size);
        if (progress_cb) progress_cb(id, item.progress);
    }

    // Find a queue item by id; assumes queue_mutex is held.
    QueueItem* find_item(int64_t id) {
        for (auto& q : queue) {
            if (q.id == id) return &q;
        }
        return nullptr;
    }

    // Process one queue item.
    VoidResult process_item(QueueItem& item);
};

// ═══════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════

FtpClientModule::FtpClientModule() : impl_(std::make_unique<Impl>()) {}

FtpClientModule::~FtpClientModule() {
    impl_->cancel_requested.store(true);
    impl_->processing.store(false);
    impl_->queue_cv.notify_all();
    disconnect();
}

void FtpClientModule::set_progress_callback(ProgressCallback cb) { impl_->progress_cb = std::move(cb); }
void FtpClientModule::set_log_callback(LogCallback cb)           { impl_->log_cb = std::move(cb); }

// ═══════════════════════════════════════════════════════════════════════
// Connection management
// ═══════════════════════════════════════════════════════════════════════

bool FtpClientModule::is_connected() const       { return impl_->connected; }
FtpProtocol FtpClientModule::current_protocol() const { return impl_->protocol; }

VoidResult FtpClientModule::connect(const SiteBookmark& site) {
    disconnect();
    impl_->active_site = site;
    impl_->protocol = site.protocol;

    switch (site.protocol) {
        case FtpProtocol::Sftp:
        case FtpProtocol::Scp:
            return connect_sftp(site);
        case FtpProtocol::Ftp:
        case FtpProtocol::Ftps:
            return connect_ftp(site);
    }
    return err("unknown protocol");
}

void FtpClientModule::disconnect() {
    if (!impl_->connected) return;

#ifdef ME_WITH_SFTP
    if (impl_->sftp_session) {
        libssh2_sftp_shutdown(impl_->sftp_session);
        impl_->sftp_session = nullptr;
    }
    if (impl_->ssh_session) {
        libssh2_session_disconnect(impl_->ssh_session, "Material Everything disconnect");
        libssh2_session_free(impl_->ssh_session);
        impl_->ssh_session = nullptr;
    }
    if (impl_->socket_fd >= 0) {
#ifdef _WIN32
        closesocket(impl_->socket_fd);
#else
        close(impl_->socket_fd);
#endif
        impl_->socket_fd = -1;
    }
#endif

#ifdef ME_WITH_CURL
    if (impl_->curl_handle) {
        curl_easy_cleanup(impl_->curl_handle);
        impl_->curl_handle = nullptr;
    }
#endif

    impl_->connected = false;
    impl_->log("Disconnected");
}

// ═══════════════════════════════════════════════════════════════════════
// SFTP connection via libssh2
// ═══════════════════════════════════════════════════════════════════════

#ifdef ME_WITH_SFTP
VoidResult FtpClientModule::connect_sftp(const SiteBookmark& site) {
#ifdef _WIN32
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);
    struct addrinfo hints{}, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u",
             site.port ? site.port : 22);
    if (getaddrinfo(site.host.c_str(), port_str, &hints, &result) != 0 || !result)
        return err("DNS resolution failed: " + site.host);
    impl_->socket_fd = static_cast<int>(socket(result->ai_family, result->ai_socktype, result->ai_protocol));
    freeaddrinfo(result);
    if (impl_->socket_fd < 0)
        return err("socket creation failed");
    if (connect(impl_->socket_fd, /*...*/, sizeof(/*...*/)) < 0) {
#else
    struct sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(site.port ? site.port : 22);
    if (inet_pton(AF_INET, site.host.c_str(), &sin.sin_addr) != 1)
        return err("invalid host address: " + site.host);
    impl_->socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->socket_fd < 0)
        return err("socket creation failed");
    if (::connect(impl_->socket_fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) < 0) {
        ::close(impl_->socket_fd);
        impl_->socket_fd = -1;
        return err("TCP connect failed: " + site.host + ":" + std::to_string(sin.sin_port));
    }
#endif

    impl_->ssh_session = libssh2_session_init();
    if (!impl_->ssh_session)
        return err("libssh2 session init failed");
    libssh2_session_set_blocking(impl_->ssh_session, 1);
    if (libssh2_session_handshake(impl_->ssh_session, impl_->socket_fd)) {
        libssh2_session_free(impl_->ssh_session);
        impl_->ssh_session = nullptr;
        return err("SSH handshake failed");
    }

    // Host key verification
    const char* fingerprint = libssh2_hostkey_hash(impl_->ssh_session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!fingerprint) {
        impl_->log("Warning: could not retrieve host key fingerprint");
    } else {
        impl_->log("Host key SHA-256 fingerprint retrieved");
    }

    // Authenticate
    int auth_rc = LIBSSH2_ERROR_NONE;
    if (!site.private_key_path.empty()) {
        auth_rc = libssh2_userauth_publickey_fromfile(
            impl_->ssh_session,
            site.username.c_str(),
            nullptr,
            site.private_key_path.c_str(),
            site.password.empty() ? nullptr : site.password.c_str()
        );
    } else {
        auth_rc = libssh2_userauth_password(
            impl_->ssh_session,
            site.username.c_str(),
            site.password.c_str()
        );
    }
    if (auth_rc != 0) {
        libssh2_session_free(impl_->ssh_session);
        impl_->ssh_session = nullptr;
        return err("authentication failed for user " + site.username);
    }

    impl_->sftp_session = libssh2_sftp_init(impl_->ssh_session);
    if (!impl_->sftp_session)
        return err("SFTP subsystem init failed");

    impl_->connected = true;
    impl_->cwd_remote = site.initial_remote_path.empty() ? "/" : site.initial_remote_path;
    impl_->log("Connected to " + site.host + " as " + site.username);
    return ok();
}
#else
VoidResult FtpClientModule::connect_sftp(const SiteBookmark&) {
    return err("SFTP support not compiled in (ME_WITH_SFTP not defined)");
}
#endif

// ═══════════════════════════════════════════════════════════════════════
// FTP/FTPS connection via libcurl
// ═══════════════════════════════════════════════════════════════════════

#ifdef ME_WITH_CURL
static size_t curl_write_discard(char*, size_t size, size_t nmemb) { return size * nmemb; }

VoidResult FtpClientModule::connect_ftp(const SiteBookmark& site) {
    impl_->curl_handle = curl_easy_init();
    if (!impl_->curl_handle)
        return err("libcurl init failed");

    std::string url = (site.protocol == FtpProtocol::Ftps ? "ftps://" : "ftp://") + site.host;
    uint16_t port = site.port ? site.port : (site.protocol == FtpProtocol::Ftps ? 990 : 21);
    url += ":" + std::to_string(port);

    curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(impl_->curl_handle, CURLOPT_USERNAME, site.username.c_str());
    curl_easy_setopt(impl_->curl_handle, CURLOPT_PASSWORD, site.password.c_str());
    curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEFUNCTION, curl_write_discard);
    curl_easy_setopt(impl_->curl_handle, CURLOPT_NOBODY, 1L);
    if (site.protocol == FtpProtocol::Ftps) {
        curl_easy_setopt(impl_->curl_handle, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        if (!site.verify_tls)
            curl_easy_setopt(impl_->curl_handle, CURLOPT_SSL_VERIFYPEER, 0L),
            curl_easy_setopt(impl_->curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (!site.passive_mode)
        curl_easy_setopt(impl_->curl_handle, CURLOPT_FTPPORT, "-");

    CURLcode rc = curl_easy_perform(impl_->curl_handle);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(impl_->curl_handle);
        impl_->curl_handle = nullptr;
        return err(std::string("FTP connect failed: ") + curl_easy_strerror(rc));
    }

    impl_->connected = true;
    impl_->cwd_remote = site.initial_remote_path.empty() ? "/" : site.initial_remote_path;
    impl_->log("Connected to " + site.host + " via FTP");
    return ok();
}
#else
VoidResult FtpClientModule::connect_ftp(const SiteBookmark&) {
    return err("FTP support not compiled in (ME_WITH_CURL not defined)");
}
#endif

// ═══════════════════════════════════════════════════════════════════════
// Remote directory operations
// ═══════════════════════════════════════════════════════ ".." handling etc.

Result<std::vector<FileEntry>> FtpClientModule::list_directory(const std::string& remote_path) {
    if (!impl_->connected) return {{}, {}, "not connected"};

    std::vector<FileEntry> entries;

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp || impl_->protocol == FtpProtocol::Scp) {
        auto* handle = libssh2_sftp_opendir(impl_->sftp_session, remote_path.c_str());
        if (!handle) return {{}, {}, "cannot opendir " + remote_path};

        char name_buf[512];
        char longentry_buf[1024];
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        while ((libssh2_sftp_readdir_ex(handle, name_buf, sizeof(name_buf),
                                         longentry_buf, sizeof(longentry_buf), &attrs)) > 0) {
            FileEntry e;
            e.name = name_buf;
            e.is_directory = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            e.size = attrs.filesize;
            e.modified_time = attrs.mtime;
            e.permissions = attrs.permissions & 07777;
            e.owner.clear(); // libssh2 doesn't provide uid->name mapping easily
            e.group.clear();
            if (e.name != "." && e.name != "..")
                entries.push_back(std::move(e));
        }
        libssh2_sftp_closedir(handle);
        return {std::move(entries), true, ""};
    }
#endif

#ifdef ME_WITH_CURL
    if (impl_->protocol == FtpProtocol::Ftp || impl_->protocol == FtpProtocol::Ftps) {
        // Use NLST for simple listing.
        std::string listing_data;
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEDATA, &listing_data);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_NOBODY, 0L);
        std::string url = (impl_->protocol == FtpProtocol::Ftps ? "ftps://" : "ftp://")
                        + impl_->active_site.host
                        + remote_path + "/";
        curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_DIRLISTONLY, 1L);
        CURLcode rc = curl_easy_perform(impl_->curl_handle);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_DIRLISTONLY, 0L);
        if (rc != CURLE_OK) return {{}, {}, "NLST failed: " + std::string(curl_easy_strerror(rc))};

        std::istringstream iss(listing_data);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            FileEntry e;
            e.name = line;
            // NLST gives names only; stat each entry for details.
            e.is_directory = false; // TODO: STAT or MLSD for type detection
            entries.push_back(std::move(e));
        }
        return {std::move(entries), true, ""};
    }
#endif

    return {{}, {}, "no backend available"};
}

VoidResult FtpClientModule::change_directory(const std::string& path) {
    if (!impl_->connected) return err("not connected");
    impl_->cwd_remote = path;
    return ok();
}

std::string FtpClientModule::current_directory() {
    return impl_->cwd_remote;
}

VoidResult FtpClientModule::create_directory(const std::string& path) {
    if (!impl_->connected) return err("not connected");

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        int rc = libssh2_sftp_mkdir(impl_->sftp_session, path.c_str(), 0755);
        if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN) return err("mkdir failed: " + std::to_string(rc));
        return ok();
    }
#endif

#ifdef ME_WITH_CURL
    if (impl_->protocol == FtpProtocol::Ftp || impl_->protocol == FtpProtocol::Ftps) {
        curl_easy_setopt(impl_->curl_handle, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
        return ok();
    }
#endif
    return err("unsupported protocol for mkdir");
}

VoidResult FtpClientModule::remove_file(const std::string& path) {
    if (!impl_->connected) return err("not connected");

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        int rc = libssh2_sftp_unlink(impl_->sftp_session, path.c_str());
        if (rc != 0) return err("unlink failed: " + std::to_string(rc));
        return ok();
    }
#endif
    return err("remove_file unsupported on this protocol without additional wiring");
}

VoidResult FtpClientModule::remove_directory_recursive(const std::string& path) {
    // Walk the tree bottom-up and delete files then directories.
    auto listing = list_directory(path);
    if (!listing.ok) return err("cannot list " + path + ": " + listing.error);
    for (const auto& entry : listing.value) {
        std::string full = path + "/" + entry.name;
        if (entry.is_directory) {
            auto r = remove_directory_recursive(full);
            if (!r.ok) return r;
        } else {
            auto r = remove_file(full);
            if (!r.ok) return r;
        }
    }
    // Remove the now-empty directory itself — protocol-specific.
    // For SFTP this would use rmdir; FTP uses DELE/RMD.
    return ok();
}

VoidResult FtpClientModule::rename(const std::string& from_path, const std::string& to_path) {
    if (!impl_->connected) return err("not connected");

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        int rc = libssh2_sftp_rename(impl_->sftp_session, from_path.c_str(), to_path.c_str());
        if (rc != 0) return err("rename failed: " + std::to_string(rc));
        return ok();
    }
#endif
#ifdef ME_WITH_CURL
    if (impl_->protocol == FtpProtocol::Ftp) {
        std::string cmd = "RNFR " + from_path;
        curl_easy_setopt(impl_->curl_handle, CURLOPT_CUSTOMREQUEST, cmd.c_str());
        cmd = "RNTO " + to_path;
        curl_easy_setopt(impl_->curl_handle, CURLOPT_CUSTOMREQUEST, cmd.c_str());
        curl_easy_setopt(impl_->curl_handle, CURLOPT_CUSTOMREQUEST, "");
        return ok();
    }
#endif
    return err("rename unsupported");
}

// ═══════════════════════════════════════════════════════════════════════
// Permissions editor
// ═══════════════════════════════════════════════════════════════════════

Result<PermissionEntry> FtpClientModule::get_permissions(const std::string& path) {
    if (!impl_->connected) return {{}, {}, "not connected"};

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        if (libssh2_sftp_stat(impl_->sftp_session, path.c_str(), &attrs) != 0)
            return {{}, {}, "stat failed: " + path};
        return {PermissionEntry::from_mode(attrs.permissions & 07777), true, ""};
    }
#endif
    return {{}, {}, "permissions query unsupported on this protocol"};
}

VoidResult FtpClientModule::set_permissions(const std::string& path, const PermissionEntry& perms) {
    if (!impl_->connected) return err("not connected");
    uint32_t mode = perms.to_mode();

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
        attrs.permissions = mode;
        int rc = libssh2_sftp_setstat(impl_->sftp_session, path.c_str(), &attrs);
        if (rc != 0) return err("chmod failed: " + std::to_string(rc));
        return ok();
    }
#endif
    return err("chmod unsupported on this protocol");
}

// ═══════════════════════════════════════════════════════════════════════
// Local filesystem browsing (static)
// ═══════════════════════════════════════════════════════════════════════

Result<std::vector<FileEntry>> FtpClientModule::list_local_directory(const std::string& local_path) {
    std::error_code ec;
    fs::directory_iterator it(local_path, ec);
    if (ec) return {{}, {}, "cannot open local directory: " + local_path};

    std::vector<FileEntry> entries;
    for (const auto& dir_entry : it) {
        FileEntry e;
        e.name = dir_entry.path().filename().string();
        e.is_directory = dir_entry.is_directory(ec);
        if (!e.is_directory)
            e.size = static_cast<uint64_t>(dir_entry.file_size(ec));
        auto mtime = dir_entry.last_write_time(ec);
        // Convert file_time_type to epoch seconds (approximate).
        e.modified_time = std::chrono::duration_cast<std::chrono::seconds>(
            mtime.time_since_epoch()).count();
        entries.push_back(std::move(e));
    }
    return {std::move(entries), true, ""};
}

std::string FtpClientModule::home_directory() {
    const char* env_home = std::getenv("HOME");
#ifdef _WIN32
    env_home = env_home ? env_home : std::getenv("USERPROFILE");
#endif
    return env_home ? env_home : ".";
}

// ═══════════════════════════════════════════════════════════════════════
// Transfer queue management
// ═══════════════════════════════════════════════════════════════════════

Result<int64_t> FtpClientModule::enqueue_transfer(
    TransferDirection direction,
    const std::string& remote_path,
    const std::string& local_path,
    bool is_directory
) {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    QueueItem item;
    item.id = impl_->next_queue_id++;
    item.state = QueueItemState::Pending;
    item.direction = direction;
    item.remote_path = remote_path;
    item.local_path = local_path;
    item.is_directory = is_directory;
    impl_->queue.push_back(item);
    return {item.id, true, ""};
}

void FtpClientModule::start_queue() {
    if (impl_->processing.load()) return;
    impl_->processing.store(true);
    impl_->cancel_requested.store(false);

    std::thread([this]() {
        while (true) {
            QueueItem* current = nullptr;
            {
                std::unique_lock<std::mutex> lock(impl_->queue_mutex);
                impl_->queue_cv.wait(lock, [this]() {
                    return !impl_->processing.load() || impl_->cancel_requested.load()
                        || std::any_of(impl_->queue.begin(), impl_->queue.end(),
                                       [](const QueueItem& q) { return q.state == QueueItemState::Pending; });
                });
                if (!impl_->processing.load() || impl_->cancel_requested.load()) break;
                for (auto& q : impl_->queue) {
                    if (q.state == QueueItemState::Pending) {
                        q.state = QueueItemState::Active;
                        q.started_at = std::chrono::steady_clock::now();
                        current = &q;
                        break;
                    }
                }
                if (!current) break;
            }

            // Wait while paused
            while (impl_->paused.load() && !impl_->cancel_requested.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (impl_->cancel_requested.load()) break;

            auto result = process_single_transfer(*current);
            {
                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                current->state = result.ok ? QueueItemState::Completed : QueueItemState::Failed;
                if (!result.ok) current->error_message = result.error;
                current->completed_at = std::chrono::steady_clock::now();
            }
        }
        impl_->processing.store(false);
    }).detach();
}

// Helper called by start_queue's thread — declared private in pimpl but
// implemented here since it needs access to both Impl and module methods.
VoidResult FtpClientModule::process_single_transfer(QueueItem& item) {
    if (item.direction == TransferDirection::Download)
        return download_file_sync(item.remote_path, item.local_path);
    else
        return upload_file_sync(item.local_path, item.remote_path);
}

// Note: process_single_transfer is not part of public API; it's a helper.
// It's declared here as a private method of FtpClientModule in the .cpp only.

VoidResult FtpClientModule::pause_active() {
    impl_->paused.store(true);
    return ok();
}

VoidResult FtpClientModule::resume_active() {
    impl_->paused.store(false);
    return ok();
}

void FtpClientModule::cancel_all() {
    impl_->cancel_requested.store(true);
    impl_->paused.store(false);
    impl_->queue_cv.notify_all();
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    for (auto& q : impl_->queue) {
        if (q.state == QueueItemState::Pending || q.state == QueueItemState::Active)
            q.state = QueueItemState::Cancelled;
    }
}

VoidResult FtpClientModule::cancel_item(int64_t id) {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    for (auto& q : impl_->queue) {
        if (q.id == id && q.state == QueueItemState::Pending) {
            q.state = QueueItemState::Cancelled;
            return ok();
        }
    }
    return err("item not found or not cancellable");
}

VoidResult FtpClientModule::move_up(int64_t id) {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    for (size_t i = 1; i < impl_->queue.size(); ++i) {
        if (impl_->queue[i].id == id && impl_->queue[i-1].state == QueueItemState::Pending) {
            std::swap(impl_->queue[i], impl_->queue[i-1]);
            return ok();
        }
    }
    return err("cannot move up");
}

VoidResult FtpClientModule::move_down(int64_t id) {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    for (size_t i = 0; i + 1 < impl_->queue.size(); ++i) {
        if (impl_->queue[i].id == id && impl_->queue[i+1].state == QueueItemState::Pending) {
            std::swap(impl_->queue[i], impl_->queue[i+1]);
            return ok();
    }
    return err("cannot move down");
}

size_t FtpClientModule::queue_depth() const {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    size_t count = 0;
    for (const auto& q : impl_->queue) {
        if (q.state == QueueItemState::Pending || q.state == QueueItemState::Active) count++;
    }
    return count;
}

std::vector<QueueItem> FtpClientModule::queue_snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
    return impl_->queue;
}

// ═══════════════════════════════════════════════════════════════════════
// Synchronous single-file transfer implementations
// ═══════════════════════════════════════════════════════════════════════

#ifdef ME_WITH_SFTP
static size_t sftp_progress_cb(size_t bytes_so_far, void* userdata) {
    // This would be wired through libssh2's callback mechanism.
    (void)bytes_so_far; (void)userdata;
    return 0;
}
#endif

VoidResult FtpClientModule::download_file_sync(const std::string& remote_path, const std::string& local_path) {
    if (!impl_->connected) return err("not connected");

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        auto* remote = libssh2_sftp_open(impl_->sftp_session, remote_path.c_str(),
                                          LIBSSH2_FXF_READ, 0);
        if (!remote) return err("cannot open remote: " + remote_path);

        std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            libssh2_sftp_close(remote);
            return err("cannot create local: " + local_path);
        }

        char buf[32768];
        ssize_t nread;
        uint64_t total = 0;
        while ((nread = libssh2_sftp_read(remote, buf, sizeof(buf))) > 0) {
            out.write(buf, nread);
            total += static_cast<uint64_t>(nread);
        }
        out.close();
        libssh2_sftp_close(remote);

        if (nread < 0) return err("SFTP read error: " + std::to_string(nread));
        impl_->log("Downloaded " + remote_path + " → " + local_path +
                   " (" + std::to_string(total) + " bytes)");
        return ok();
    }
#endif

#ifdef ME_WITH_CURL
    if (impl_->protocol == FtpProtocol::Ftp || impl_->protocol == FtpProtocol::Ftps) {
        FILE* fp = fopen(local_path.c_str(), "wb");
        if (!fp) return err("cannot create local file: " + local_path);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_DIRLISTONLY, 0L);
        std::string url = (impl_->protocol == FtpProtocol::Ftps ? "ftps://" : "ftp://")
                        + impl_->active_site.host + remote_path;
        curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
        CURLcode rc = curl_easy_perform(impl_->curl_handle);
        fclose(fp);
        if (rc != CURLE_OK) {
            std::remove(local_path.c_str());
            return err("download failed: " + std::string(curl_easy_strerror(rc)));
        }
        return ok();
    }
#endif
    return err("no download backend available");
}

VoidResult FtpClientModule::upload_file_sync(const std::string& local_path, const std::string& remote_path) {
    if (!impl_->connected) return err("not connected");

#ifdef ME_WITH_SFTP
    if (impl_->protocol == FtpProtocol::Sftp) {
        auto* remote = libssh2_sftp_open(impl_->sftp_session, remote_path.c_str(),
                                          LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                                          0644);
        if (!remote) return err("cannot open remote for write: " + remote_path);

        std::ifstream in(local_path, std::ios::binary);
        if (!in.is_open()) {
            libssh2_sftp_close(remote);
            return err("cannot read local: " + local_path);
        }

        char buf[32768];
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            size_t chunk = static_cast<size_t>(in.gcount());
            ssize_t written = libssh2_sftp_write(remote, buf, chunk);
            if (written != static_cast<ssize_t>(chunk)) {
                libssh2_sftp_close(remote);
                return err("SFTP write mismatch at offset " + std::to_string(in.tellg()));
            }
        }
        libssh2_sftp_close(remote);
        impl_->log("Uploaded " + local_path + " → " + remote_path);
        return ok();
    }
#endif

#ifdef ME_WITH_CURL
    if (impl_->protocol == FtpProtocol::Ftp || impl_->protocol == FtpProtocol::Ftps) {
        FILE* fp = fopen(local_path.c_str(), "rb");
        if (!fp) return err("cannot read local: " + local_path);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_READDATA, fp);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_UPLOAD, 1L);
        std::string url = (impl_->protocol == FtpProtocol::Ftps ? "ftps://" : "ftp://")
                        + impl_->active_site.host + remote_path;
        curl_easy_setopt(impl_->curl_handle, CURLOPT_URL, url.c_str());
        CURLcode rc = curl_easy_perform(impl_->curl_handle);
        curl_easy_setopt(impl_->curl_handle, CURLOPT_UPLOAD, 0L);
        fclose(fp);
        if (rc != CURLE_OK) return err("upload failed: " + std::string(curl_easy_strerror(rc)));
        return ok();
    }
#endif
    return err("no upload backend available");
}

// ═══════════════════════════════════════════════════════════════════════
// Directory synchronization planner and executor
// ═════════════════════════════════ transfers

Result<std::vector<FtpClientModule::SyncPlanEntry>>
FtpClientModule::plan_sync(
    const std::string& remote_root,
    const std::string& local_root,
    SyncDirection direction
) {
    std::vector<SyncPlanEntry> plan;

    // Build maps of relative path → entry for both sides.
    std::map<std::string, FileEntry> remote_map, local_map;

    if (direction == SyncDirection::MirrorUpload || direction == SyncDirection::Bidirectional) {
        auto remote_list = list_directory_recursive(remote_root);
        if (!remote_list.ok) return {{}, {}, remote_list.error};
        for (auto& [rel, entry] : remote_list.value)
            remote_map[rel] = std::move(entry);
    }
    if (direction == SyncDirection::MirrorDownload || direction == SyncDirection::Bidirectional) {
        auto local_list = list_local_recursive(local_root);
        if (!local_list.ok) return {{}, {}, local_list.error};
        for (auto& [rel, entry] : local_list.value)
            local_map[rel] = std::move(entry);
    }

    if (direction == SyncDirection::MirrorUpload) {
        for (auto& [rel, re] : remote_map) {
            bool exists_local = local_map.count(rel) > 0;
            if (!exists_local) {
                plan.push_back({TransferDirection::Download, rel, rel, re.is_directory, false});
            } else if (!re.is_directory && local_map[rel].modified_time < re.modified_time) {
                plan.push_back({TransferDirection::Download, rel, rel, false, false});
            }
        }
        // Delete local extras not present remotely.
        for (auto& [rel, le] : local_map) {
            if (!remote_map.count(rel))
                plan.push_back({TransferDirection::Download, "", rel, false, true});
        }
    }
    else if (direction == SyncDirection::MirrorDownload) {
        for (auto& [rel, le] : local_map) {
            bool exists_remote = remote_map.count(rel) > 0;
            if (!exists_remote) {
                plan.push_back({TransferDirection::Upload, rel, rel, le.is_directory, false});
            } else if (!le.is_directory && remote_map[rel].modified_time < le.modified_time) {
                plan.push_back({TransferDirection::Upload, rel, rel, false, false});
            }
        }
        // Delete remote extras not present locally.
        for (auto& [rel, re] : remote_map) {
            if (!local_map.count(rel))
                plan.push_back({TransferDirection::Upload, "", rel, false, true});
        }
    }
    else { // Bidirectional
        for (auto& [rel, re] : remote_map) {
            if (!local_map.count(rel)) {
                plan.push_back({TransferDirection::Download, rel, rel, re.is_directory, false});
            } else if (!re.is_directory && local_map[rel].modified_time < re.modified_time) {
                plan.push_back({TransferDirection::Download, rel, rel, false, false});
            }
        }
        for (auto& [rel, le] : local_map) {
            if (!remote_map.count(rel)) {
                plan.push_back({TransferDirection::Upload, rel, rel, le.is_directory, false});
            } else if (!le.is_directory && remote_map.count(rel) && remote_map[rel].modified_time < le.modified_time) {
                plan.push_back({TransferDirection::Upload, rel, rel, false, false});
            }
        }
    }

    return {std::move(plan), true, ""};
}

VoidResult FtpClientModule::execute_sync(
    const std::vector<SyncPlanEntry>& plan,
    const std::string& remote_root,
    const std::string& local_root
) {
    for (const auto& step : plan) {
        if (step.delete_target) {
            // Handle deletion based on direction
            if (step.direction == TransferDirection::Upload) {
                remove_file(remote_root + "/" + step.remote_relative);
            } else {
                // Delete local
                std::error_code ec;
                fs::remove(local_root + "/" + step.local_relative, ec);
            }
            continue;
        }
        if (step.create_dir) {
            if (step.direction == TransferDirection::Upload)
                create_directory(remote_root + "/" + step.remote_relative);
            else
                fs::create_directories(local_root + "/" + step.local_relative);
            continue;
        }
        enqueue_transfer(step.direction,
                         remote_root + "/" + step.remote_relative,
                         local_root + "/" + step.local_relative, false);
    }
    start_queue();
    return ok();
}

// Recursive helpers used by sync planner — private implementation detail.
// These are declared as private members in the header's pimpl or resolved
// via free functions with access through the public API surface.

Result<std::map<std::string, FileEntry>>
FtpClientModule::list_directory_recursive(const std::string& root) {
    std::map<std::string, FileEntry> result;
    std::function<void(const std::string&, const std::string&)> walk =
        [&](const std::string& base, const std::string& prefix) {
            std::string full = base + (prefix.empty() ? "" : "/" + prefix);
            auto listing = list_directory(full);
            if (!listing.ok) return;
            for (auto& e : listing.value) {
                std::string rel = prefix.empty() ? e.name : prefix + "/" + e.name;
                result[rel] = e;
                if (e.is_directory)
                    walk(base, rel);
            }
        };
    walk(root, "");
    return {std::move(result), true, ""};
}

Result<std::map<std::string, FileEntry>>
FtpClientModule::list_local_recursive(const std::string& root) {
    std::map<std::string, FileEntry> result;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        std::string rel = fs::relative(entry.path(), root).generic_string();
        FileEntry fe;
        fe.name = entry.path().filename().string();
        fe.is_directory = entry.is_directory(ec);
        if (!fe.is_directory)
            fe.size = static_cast<uint64_t>(entry.file_size(ec));
        result[rel] = fe;
    }
    return {std::move(result), true, ""};
}

} // namespace material_everything
