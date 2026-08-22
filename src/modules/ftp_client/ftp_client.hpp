#pragma once
/*
 * Material Everything — FTP/SFTP Client Module
 * Public API header. Self-contained; no external UI framework dependency.
 *
 * Features:
 *   - FTP, FTPS (explicit), SFTP, and SCP protocol support
 *   - Dual-pane local/remote browsing with independent navigation
 *   - Drag-and-drop transfer initiation between panes
 *   - Transfer queue management (pause/resume/cancel/reorder)
 *   - Bookmarked remote sites with stored connection profiles
 *   - Directory synchronization (mirror upload/download/bidirectional)
 *   - Remote file permission editor (POSIX mode bits via chmod)
 *   - Transfer progress callbacks with byte-accurate reporting
 *
 * Backend: libssh2 for SFTP/SCP; libcurl for FTP/FTPS.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace material_everything {

// ── Connection protocol enum ────────────────────────────────────────────

enum class FtpProtocol {
    Ftp,
    Ftps,
    Sftp,
    Scp
};

// ── Transfer direction ──────────────────────────────────────────────────

enum class TransferDirection {
    Upload,
    Download
};

// ── Sync direction ──────────────────────────────────────────────────────

enum class SyncDirection {
    MirrorUpload,
    MirrorDownload,
    Bidirectional
};

// ── Transfer queue item state ───────────────────────────────────────────

enum class QueueItemState {
    Pending,
    Active,
    Paused,
    Completed,
    Failed,
    Cancelled
};

// ── File entry (remote or local) ────────────────────────────────────────

struct FileEntry {
    std::string name;
    bool is_directory = false;
    uint64_t size = 0;
    uint64_t modified_time = 0; // Unix epoch seconds
    uint32_t permissions = 0;   // POSIX mode bits (e.g. 0644)
    std::string owner;
    std::string group;
};

// ── Connection profile / bookmark ───────────────────────────────────────

struct SiteBookmark {
    int64_t id = 0;
    std::string label;
    FtpProtocol protocol = FtpProtocol::Ftp;
    std::string host;
    uint16_t port = 0;         // 0 → protocol default
    std::string username;
    std::string password;      // caller manages storage security
    std::string private_key_path;
    std::string known_hosts_path;
    std::string initial_remote_path;
    std::string initial_local_path;
    bool passive_mode = true;
    bool verify_tls = true;
};

// ── Transfer progress snapshot ──────────────────────────────────────────

struct TransferProgress {
    uint64_t bytes_transferred = 0;
    uint64_t total_bytes = 0;
    double speed_bytes_per_sec = 0.0;
    double eta_seconds = 0.0;
    double percent = 0.0;
};

// ── Transfer queue item ─────────────────────────────────────────────────

struct QueueItem {
    int64_t id = 0;
    QueueItemState state = QueueItemState::Pending;
    TransferDirection direction = TransferDirection::Download;
    std::string remote_path;
    std::string local_path;
    bool is_directory = false;
    uint64_t total_size = 0;
    TransferProgress progress;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point completed_at;
    std::string error_message;
};

// ── Permission entry for the editor ─────────────────────────────────────

struct PermissionEntry {
    std::string path;
    uint32_t owner_read  : 1 = 0;
    uint32_t owner_write : 1 = 0;
    uint32_t owner_exec  : 1 = 0;
    uint32_t group_read  : 1 = 0;
    uint32_t group_write : 1 = 0;
    uint32_t group_exec  : 1 = 0;
    uint32_t other_read  : 1 = 0;
    uint32_t other_write : 1 = 0;
    uint32_t other_exec  : 1 = 0;
    uint32_t setuid      : 1 = 0;
    uint32_t setgid      : 1 = 0;
    uint32_t sticky      : 1 = 0;

    uint32_t to_mode() const {
        return (owner_read << 8) | (owner_write << 7) | (owner_exec << 6)
             | (group_read << 5) | (group_write << 4) | (group_exec << 3)
             | (other_read << 2) | (other_write << 1) | other_exec
             | (setuid << 11) | (setgid << 10) | (sticky << 9);
    }

    static PermissionEntry from_mode(uint32_t m) {
        PermissionEntry p;
        p.owner_read  = (m >> 8) & 1;
        p.owner_write = (m >> 7) & 1;
        p.owner_exec  = (m >> 6) & 1;
        p.group_read  = (m >> 5) & 1;
        p.group_write = (m >> 4) & 1;
        p.group_exec  = (m >> 3) & 1;
        p.other_read  = (m >> 2) & 1;
        p.other_write = (m >> 1) & 1;
        p.other_exec  = m & 1;
        p.setuid      = (m >> 11) & 1;
        p.setgid      = (m >> 10) & 1;
        p.sticky      = (m >> 9) & 1;
        return p;
    }
};

// ── Result type ─────────────────────────────────────────────────────────

template <typename T>
struct Result {
    bool ok = false;
    T value{};
    std::string error;
};

using VoidResult = Result<bool>;

inline VoidResult ok() { return {true, true, ""}; }
inline VoidResult err(const std::string& msg) { return {false, false, msg}; }

// ── Progress callback type ──────────────────────────────────────────────

using ProgressCallback = std::function<void(int64_t queue_item_id, const TransferProgress&)>;
using LogCallback     = std::function<void(const std::string& message)>;

// ── Main FTP/SFTP client class ──────────────────────────────────────────

class FtpClientModule {
public:
    explicit FtpClientModule();
    ~FtpClientModule();

    // Non-copyable.
    FtpClientModule(const FtpClientModule&) = delete;
    FtpClientModule& operator=(const FtpClientModule&) = delete;

    // ── Lifecycle ───────────────────────────────────────────────────────

    void set_progress_callback(ProgressCallback cb);
    void set_log_callback(LogCallback cb);

    // ── Connection management ───────────────────────────────────────────

    // Connect to a site. Returns error string on failure or "" on success.
    VoidResult connect(const SiteBookmark& site);
    void disconnect();
    bool is_connected() const;
    FtpProtocol current_protocol() const;

    // ── Remote operations ───────────────────────────────────────────────

    Result<std::vector<FileEntry>> list_directory(const std::string& remote_path);
    VoidResult change_directory(const std::string& remote_path);
    std::string current_directory();
    VoidResult create_directory(const std::string& remote_path);
    VoidResult remove_file(const std::string& remote_path);
    VoidResult remove_directory_recursive(const std::string& remote_path);
    VoidResult rename(const std::string& from_path, const std::string& to_path);

    // Permissions editor backend
    Result<PermissionEntry> get_permissions(const std::string& remote_path);
    VoidResult set_permissions(const std::string& remote_path, const PermissionEntry& perms);

    // ── Local filesystem browsing ───────────────────────────────────────

    static Result<std::vector<FileEntry>> list_local_directory(const std::string& local_path);
    static std::string home_directory();

    // ── Transfers ───────────────────────────────────────────────────────

    // Enqueue a single file or directory transfer.
    Result<int64_t> enqueue_transfer(
        TransferDirection direction,
        const std::string& remote_path,
        const std::string& local_path,
        bool is_directory
    );

    // Start processing the transfer queue.
    void start_queue();

    // Pause the active item (resumable downloads only; uploads restart).
    VoidResult pause_active();

    // Resume a paused item.
    VoidResult resume_active();

    // Cancel active and all pending items.
    void cancel_all();

    // Remove one pending item from the queue by id.
    VoidResult cancel_item(int64_t id);

    // Reorder: move an item up or down in the pending list.
    VoidResult move_up(int64_t id);
    VoidResult move_down(int64_t id);

    size_t queue_depth() const;
    std::vector<QueueItem> queue_snapshot() const;

    // Synchronous single-file transfer (bypasses queue).
    VoidResult download_file_sync(const std::string& remote_path, const std::string& local_path);
    VoidResult upload_file_sync(const std::string& local_path, const std::string& remote_path);

    // ── Directory sync ──────────────────────────────────────────────────

    struct SyncPlanEntry {
        TransferDirection direction;
        std::string remote_relative;
        std::string local_relative;
        bool create_dir = false;
        bool delete_target = false;
    };

    Result<std::vector<SyncPlanEntry>> plan_sync(
        const std::string& remote_root,
        const std::string& local_root,
        SyncDirection direction
    );

    // Execute a sync plan through the queue.
    VoidResult execute_sync(const std::vector<SyncPlanEntry>& plan,
                            const std::string& remote_root,
                            const std::string& local_root);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace material_everything
