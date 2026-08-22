#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace material_everything::download_manager {

enum class TransferState {
    Queued,
    Connecting,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

enum class FileCategory {
    Unknown,
    Document,
    Image,
    Audio,
    Video,
    Archive,
    Application,
};

struct SegmentProgress {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t received = 0;
};

struct DownloadItem {
    std::string id;
    std::string url;
    std::string suggestedName = "download";
    std::filesystem::path destinationDirectory;
    std::uintmax_t totalBytes = 0;
    std::uintmax_t receivedBytes = 0;
    int segmentCount = 4;
    int speedLimitBytesPerSecond = 0;
    FileCategory category = FileCategory::Unknown;
    TransferState state = TransferState::Queued;
    std::string statusMessage;
    std::chrono::system_clock::time_point createdAt{};
    std::chrono::steady_clock::time_point lastActivityAt{};
    std::vector<SegmentProgress> segments;
};

struct MaterialDownloadCard {
    std::string itemId;
    std::string title;
    std::string subtitle;
    double fractionComplete = 0.0;
    std::string progressLabel;
    std::string speedLabel;
    std::string categoryLabel;
    TransferState state = TransferState::Queued;
    bool isIndeterminate = true;
    std::string supportingText;
    std::string actionLabel;
    std::string secondaryActionLabel;
    std::string stateIconGlyph;
    std::string accentToken;
};

struct CompletionNotification {
    std::string title;
    std::string body;
    std::string itemId;
    bool succeeded = false;
    std::chrono::system_clock::time_point timestamp{};
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    struct Response {
        long statusCode = 0;
        std::string contentType;
        std::optional<std::string> contentDispositionFilename;
        std::optional<std::int64_t> contentLength;
        bool supportsByteRanges = false;
    };

    virtual Response inspect(const std::string& url) = 0;
    virtual bool downloadRange(const std::string& url, std::int64_t start, std::int64_t end,
                               const std::filesystem::path& destination,
                               const std::function<bool(std::int64_t)>& onBytesReceived) = 0;
};

class DownloadManager {
public:
    static FileCategory classifyFile(const std::string& filename);
    static std::string categoryName(FileCategory category);
    static std::string formatBytes(std::uintmax_t value);
    static int nextIdCounter();

    explicit DownloadManager(std::unique_ptr<HttpClient> client,
                             std::filesystem::path destinationRoot =
                                 std::filesystem::current_path() / "Material Downloads");
    ~DownloadManager();

    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;
    DownloadManager(DownloadManager&&) = delete;
    DownloadManager& operator=(DownloadManager&&) = delete;

    void setSpeedLimitBytesPerSecond(int bytesPerSecond);
    int speedLimitBytesPerSecond() const;

    std::string enqueue(const std::string& url, int segments = 4);
    void enqueueBatch(const std::vector<std::string>& urls, int segmentsPerUrl = 4);
    bool pause(const std::string& id);
    bool resume(const std::string& id);
    bool cancel(const std::string& id);
    bool clearCompleted();

    std::vector<DownloadItem> items() const;
    std::optional<DownloadItem> item(const std::string& id) const;
    std::size_t queuedCount() const;
    std::size_t activeCount() const;

    // Clipboard integration hook. Returns the accepted URL, if any.
    std::optional<std::string> acceptClipboardText(const std::string& text);

    std::vector<CompletionNotification> takeNotifications();

private:
    struct Worker {
        std::thread thread;
    };

    void runWorker(int workerIndex);
    bool processNext(std::size_t workerIndex);
    void runTransfer(DownloadItem& item, Worker& worker);
    static bool looksLikeUrl(const std::string& text);
    static std::string newId();

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::deque<std::string> queue_;
    std::map<std::string, std::shared_ptr<DownloadItem>> items_;
    std::map<int, Worker> workers_;
    std::unique_ptr<HttpClient> httpClient_;
    std::filesystem::path destinationRoot_;
    int globalSpeedLimit_ = 0;
    std::vector<CompletionNotification> notifications_;
    std::atomic_bool stopping_{false};
};

MaterialDownloadCard makeMaterialCard(const DownloadItem& item);

}  // namespace material_everything::download_manager
