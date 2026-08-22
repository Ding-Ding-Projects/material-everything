#include "download_manager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace material_everything::download_manager {
namespace {

std::atomic_int idCounter{0};

}  // namespace

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool containsScheme(const std::string& text) {
    return text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0 ||
           text.rfind("ftp://", 0) == 0 || text.rfind("magnet:", 0) == 0;
}

}  // namespace

int DownloadManager::nextIdCounter() {
    return idCounter.fetch_add(1, std::memory_order_relaxed) + 1;
}

DownloadManager::DownloadManager(std::unique_ptr<HttpClient> client,
                                 std::filesystem::path destinationRoot)
    : httpClient_(std::move(client)), destinationRoot_(std::move(destinationRoot)) {
    std::error_code ignored;
    std::filesystem::create_directories(destinationRoot_, ignored);

    constexpr int workerCount = 3;
    for (int index = 0; index < workerCount; ++index) {
        Worker worker;
        worker.thread = std::thread([this, index] { runWorker(index); });
        workers_.emplace(index, std::move(worker));
    }
}

DownloadManager::~DownloadManager() {
    {
        std::lock_guard lock(mutex_);
        stopping_.store(true, std::memory_order_release);
        for (auto& entry : items_) {
            auto& item = *entry.second;
            if (item.state == TransferState::Downloading || item.state == TransferState::Paused) {
                item.state = TransferState::Cancelled;
            }
        }
    }
    workAvailable_.notify_all();
    for (auto& entry : workers_) {
        if (entry.second.thread.joinable()) {
            entry.second.thread.join();
        }
    }
}

void DownloadManager::setSpeedLimitBytesPerSecond(int bytesPerSecond) {
    std::lock_guard lock(mutex_);
    globalSpeedLimit_ = std::clamp(bytesPerSecond, 0, 1'000'000'000);
}

int DownloadManager::speedLimitBytesPerSecond() const {
    std::lock_guard lock(mutex_);
    return globalSpeedLimit_;
}

bool DownloadManager::looksLikeUrl(const std::string& text) {
    std::istringstream stream(text);
    std::string candidate;
    while (stream >> candidate) {
        if (containsScheme(candidate)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> DownloadManager::acceptClipboardText(const std::string& text) {
    if (!looksLikeUrl(text)) {
        return std::nullopt;
    }
    std::istringstream stream(text);
    std::string candidate;
    std::string accepted;
    while (stream >> candidate) {
        if (containsScheme(candidate)) {
            accepted = candidate;
            break;
        }
    }
    enqueue(accepted);
    return accepted;
}

std::string DownloadManager::newId() {
    using namespace std::chrono;
    const auto now = steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << "dl-" << now << "-" << nextIdCounter();
    return out.str();
}

std::string DownloadManager::formatBytes(std::uintmax_t value) {
    constexpr std::uintmax_t kib = 1024;
    if (value < kib) {
        return std::to_string(value) + " B";
    }
    if (value < kib * kib) {
        std::ostringstream out;
        out.precision(1);
        out << std::fixed << static_cast<double>(value) / kib << " KiB";
        return out.str();
    }
    if (value < kib * kib * kib) {
        std::ostringstream out;
        out.precision(1);
        out << std::fixed << static_cast<double>(value) / (kib * kib) << " MiB";
        return out.str();
    }
    std::ostringstream out;
    out.precision(2);
    out << std::fixed << static_cast<double>(value) / (kib * kib * kib) << " GiB";
    return out.str();
}

std::string DownloadManager::categoryName(FileCategory category) {
    switch (category) {
        case FileCategory::Document: return "Document";
        case FileCategory::Image: return "Image";
        case FileCategory::Audio: return "Audio";
        case FileCategory::Video: return "Video";
        case FileCategory::Archive: return "Archive";
        case FileCategory::Application: return "Application";
        case FileCategory::Unknown: break;
    }
    return "Other";
}

FileCategory DownloadManager::classifyFile(const std::string& filename) {
    const auto lowered = toLower(filename);
    const auto dot = lowered.rfind('.');
    if (dot == std::string::npos) {
        return FileCategory::Unknown;
    }
    const std::string extension = lowered.substr(dot + 1);
    const std::initializer_list<const char*> documents = {"pdf", "doc", "docx", "txt", "md",
                                                          "rtf"};
    const std::initializer_list<const char*> images = {"png", "jpg", "jpeg", "gif", "webp",
                                                       "svg", "bmp"};
    const std::initializer_list<const char*> audio = {"mp3", "flac", "wav", "ogg", "m4a"};
    const std::initializer_list<const char*> videos = {"mp4", "mkv", "mov", "avi", "webm"};
    const std::initializer_list<const char*> archives = {"zip", "7z", "tar", "gz", "xz", "rar"};
    const std::initializer_list<const char*> applications = {"exe", "msi", "appimage", "dmg",
                                                             "apk"};
    const auto hasAny = [&](const auto& list) {
        return std::any_of(list.begin(), list.end(),
                           [&](const char* value) { return extension == value; });
    };
    if (hasAny(documents)) return FileCategory::Document;
    if (hasAny(images)) return FileCategory::Image;
    if (hasAny(audio)) return FileCategory::Audio;
    if (hasAny(videos)) return FileCategory::Video;
    if (hasAny(archives)) return FileCategory::Archive;
    if (hasAny(applications)) return FileCategory::Application;
    return FileCategory::Unknown;
}

std::string DownloadManager::enqueue(const std::string& url, int segments) {
    if (!containsScheme(url)) {
        throw std::invalid_argument("A download URL must use http, https, ftp, or magnet");
    }
    auto item = std::make_shared<DownloadItem>();
    item->id = newId();
    item->url = url;
    item->segmentCount = std::clamp(segments, 1, 16);
    item->destinationDirectory = destinationRoot_;
    const auto slash = url.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < url.size()) {
        std::string tail = url.substr(slash + 1);
        const auto query = tail.find_first_of("?#");
        if (query != std::string::npos) {
            tail.resize(query);
        }
        if (!tail.empty()) {
            item->suggestedName = tail;
        }
    }
    item->category = classifyFile(item->suggestedName);
    item->createdAt = std::chrono::system_clock::now();

    {
        std::lock_guard lock(mutex_);
        queue_.push_back(item->id);
        items_[item->id] = item;
    }
    workAvailable_.notify_one();
    return item->id;
}

void DownloadManager::enqueueBatch(const std::vector<std::string>& urls, int segmentsPerUrl) {
    for (const auto& url : urls) {
        enqueue(url, segmentsPerUrl);
    }
}

std::optional<DownloadItem> DownloadManager::item(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto found = items_.find(id);
    return found == items_.end() ? std::nullopt : std::optional(*found->second);
}

std::vector<DownloadItem> DownloadManager::items() const {
    std::lock_guard lock(mutex_);
    std::vector<DownloadItem> result;
    result.reserve(items_.size());
    for (const auto& entry : items_) {
        result.push_back(*entry.second);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.createdAt > right.createdAt;
    });
    return result;
}

std::size_t DownloadManager::queuedCount() const {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

std::size_t DownloadManager::activeCount() const {
    std::lock_guard lock(mutex_);
    return std::count_if(items_.begin(), items_.end(), [](const auto& entry) {
        return entry.second->state == TransferState::Downloading ||
               entry.second->state == TransferState::Connecting;
    });
}

void DownloadManager::runWorker(int workerIndex) {
    while (!stopping_.load(std::memory_order_acquire)) {
        std::unique_lock lock(mutex_);
        workAvailable_.wait(lock, [&] {
            return stopping_.load(std::memory_order_acquire) ||
                   std::any_of(items_.begin(), items_.end(), [](const auto& entry) {
                       return entry.second->state == TransferState::Queued;
                   });
        });
        if (stopping_.load(std::memory_order_acquire)) {
            return;
        }
        lock.unlock();
        processNext(static_cast<std::size_t>(workerIndex));
    }
}

bool DownloadManager::processNext(std::size_t workerIndex) {
    std::unique_lock lock(mutex_);
    auto selected = items_.end();
    for (auto it = items_.begin(); it != items_.end(); ++it) {
        if (it->second->state != TransferState::Queued) continue;
        const auto inQueue = std::find(queue_.begin(), queue_.end(), it->first) != queue_.end();
        if (inQueue) {
            selected = it;
            break;
        }
    }
    if (selected == items_.end()) {
        return false;
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), selected->first), queue_.end());
    auto& item = *selected->second;
    item.state = TransferState::Connecting;
    item.lastActivityAt = std::chrono::steady_clock::now();
    Worker& worker = workers_.at(static_cast<int>(workerIndex));
    lock.unlock();

    runTransfer(item, worker);

    lock.lock();
    if (item.state != TransferState::Cancelled) {
        workAvailable_.notify_all();
    }
    return true;
}

void DownloadManager::runTransfer(DownloadItem& item, Worker& worker) {
    (void)worker;
    try {
        auto response = httpClient_->inspect(item.url);
        if (response.statusCode < 200 || response.statusCode >= 300) {
            item.state = TransferState::Failed;
            item.statusMessage = "The server answered HTTP " + std::to_string(response.statusCode);
            return;
        }
        if (response.contentDispositionFilename) {
            item.suggestedName = *response.contentDispositionFilename;
            item.category = classifyFile(item.suggestedName);
        }
        item.totalBytes = response.contentLength ? static_cast<std::uintmax_t>(*response.contentLength)
                                                 : 0;

        std::filesystem::path destination = item.destinationDirectory / item.suggestedName;
        int suffix = 1;
        while (std::filesystem::exists(destination)) {
            destination = item.destinationDirectory /
                          ("copy-" + std::to_string(suffix++) + "-" + item.suggestedName);
        }

        item.segments.assign(static_cast<std::size_t>(item.segmentCount), {});
        if (item.totalBytes > 0 && response.supportsByteRanges && item.segmentCount > 1) {
            const std::int64_t span = static_cast<std::int64_t>(item.totalBytes / item.segmentCount);
            std::int64_t cursor = 0;
            for (int index = 0; index < item.segmentCount; ++index) {
                auto& segment = item.segments[static_cast<std::size_t>(index)];
                segment.start = cursor;
                segment.end = index == item.segmentCount - 1 ? static_cast<std::int64_t>(item.totalBytes) - 1
                                                             : cursor + span - 1;
                cursor += span;
            }
        } else {
            item.segmentCount = 1;
            item.segments.assign(1, {0, static_cast<std::int64_t>(std::max<std::uintmax_t>(item.totalBytes, 1)) - 1});
        }

        item.state = TransferState::Downloading;
        std::atomic_size_t completedSegments{0};
        std::vector<std::thread> rangeThreads;
        std::exception_ptr firstError;
        std::mutex errorMutex;
        std::atomic_bool cancelled{false};

        for (int index = 0; index < item.segmentCount; ++index) {
            const int segmentIndex = index;
            rangeThreads.emplace_back([&, segmentIndex] {
                auto& segment = item.segments[static_cast<std::size_t>(segmentIndex)];
                const auto partPath = destination.string() + ".part" +
                                      std::to_string(segmentIndex);
                const bool ok = httpClient_->downloadRange(
                    item.url, segment.start, segment.end, partPath,
                    [&](std::int64_t bytes) {
                        std::lock_guard stateLock(mutex_);
                        segment.received += bytes;
                        item.receivedBytes += bytes;
                        item.lastActivityAt = std::chrono::steady_clock::now();
                        if (cancelled.load(std::memory_order_acquire)) {
                            return false;
                        }
                        if (item.state == TransferState::Paused) {
                            // A paused transfer stops consuming bytes but keeps its parts.
                            return false;
                        }
                        return true;
                    });
                if (!ok) {
                    cancelled.store(true, std::memory_order_release);
                    std::lock_guard errorLock(errorMutex);
                    if (!firstError) {
                        firstError = std::make_exception_ptr(
                            std::runtime_error("A byte-range request stopped early"));
                    }
                    return;
                }
                ++completedSegments;
            });
        }
        for (auto& thread : rangeThreads) {
            thread.join();
        }

        if (cancelled.load(std::memory_order_acquire)) {
            std::lock_guard errorLock(errorMutex);
            if (firstError) {
                std::rethrow_exception(firstError);
            }
        }
        if (completedSegments.load() != static_cast<std::size_t>(item.segmentCount)) {
            throw std::runtime_error("Not every segment finished");
        }

        std::ofstream combined(destination, std::ios::binary | std::ios::trunc);
        if (!combined) {
            throw std::runtime_error("Could not open the final file for writing");
        }
        for (int index = 0; index < item.segmentCount; ++index) {
            const auto partPath = destination.string() + ".part" + std::to_string(index);
            std::ifstream part(partPath, std::ios::binary);
            if (!part) {
                throw std::runtime_error("A downloaded part disappeared before assembly");
            }
            combined << part.rdbuf();
            if (!combined) {
                throw std::runtime_error("Writing the assembled file failed");
            }
        }
        combined.close();
        for (int index = 0; index < item.segmentCount; ++index) {
            std::error_code ignored;
            std::filesystem::remove(destination.string() + ".part" + std::to_string(index), ignored);
        }

        item.state = TransferState::Completed;
        CompletionNotification note;
        note.itemId = item.id;
        note.title = "Download complete";
        note.body = item.suggestedName + " · " + formatBytes(item.totalBytes) +
                    " saved to " + item.destinationDirectory.string();
        note.succeeded = true;
        note.timestamp = std::chrono::system_clock::now();
        notifications_.push_back(note);
    } catch (const std::exception& error) {
        item.state = TransferState::Failed;
        item.statusMessage = error.what();
        CompletionNotification note;
        note.itemId = item.id;
        note.title = "Download failed";
        note.body = item.suggestedName + " stopped: " + error.what();
        note.succeeded = false;
        note.timestamp = std::chrono::system_clock::now();
        notifications_.push_back(note);
    }
}

bool DownloadManager::pause(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = items_.find(id);
    if (found == items_.end()) {
        return false;
    }
    auto& item = *found->second;
    if (item.state == TransferState::Downloading || item.state == TransferState::Connecting) {
        item.state = TransferState::Paused;
        return true;
    }
    if (item.state == TransferState::Queued) {
        item.state = TransferState::Paused;
        queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
        return true;
    }
    return false;
}

bool DownloadManager::resume(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = items_.find(id);
    if (found == items_.end() || found->second->state != TransferState::Paused) {
        return false;
    }
    auto& item = *found->second;
    item.state = TransferState::Queued;
    queue_.push_back(id);
    workAvailable_.notify_one();
    return true;
}

bool DownloadManager::cancel(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = items_.find(id);
    if (found == items_.end()) {
        return false;
    }
    auto& item = *found->second;
    switch (item.state) {
        case TransferState::Queued:
        case TransferState::Paused:
            queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
            break;
        case TransferState::Connecting:
        case TransferState::Downloading:
            break;
        default:
            return false;
    }
    item.state = TransferState::Cancelled;
    return true;
}

bool DownloadManager::clearCompleted() {
    std::lock_guard lock(mutex_);
    const auto before = items_.size();
    for (auto it = items_.begin(); it != items_.end();) {
        if (it->second->state == TransferState::Completed) {
            it = items_.erase(it);
        } else {
            ++it;
        }
    }
    return items_.size() != before;
}

std::vector<CompletionNotification> DownloadManager::takeNotifications() {
    std::lock_guard lock(mutex_);
    return std::exchange(notifications_, {});
}

MaterialDownloadCard makeMaterialCard(const DownloadItem& item) {
    MaterialDownloadCard card;
    card.itemId = item.id;
    card.title = item.suggestedName;
    card.subtitle = item.url;
    card.categoryLabel = DownloadManager::categoryName(item.category);
    card.state = item.state;
    card.isIndeterminate = item.totalBytes == 0;
    card.fractionComplete =
        item.totalBytes > 0 ? static_cast<double>(item.receivedBytes) / item.totalBytes : 0.0;
    card.progressLabel = DownloadManager::formatBytes(item.receivedBytes);
    card.speedLabel = item.speedLimitBytesPerSecond > 0
                          ? "limited to " + DownloadManager::formatBytes(item.speedLimitBytesPerSecond) + "/s"
                          : "";
    card.actionLabel = item.state == TransferState::Downloading ? "Pause" : "Resume";
    card.secondaryActionLabel = "Cancel";
    switch (item.state) {
        case TransferState::Queued: card.stateIconGlyph = "hourglass_top"; card.accentToken = "--md-sys-color-tertiary"; break;
        case TransferState::Connecting: card.stateIconGlyph = "cloud_sync"; card.accentToken = "--md-sys-color-primary"; break;
        case TransferState::Downloading: card.stateIconGlyph = "download"; card.accentToken = "--md-sys-color-primary"; break;
        case TransferState::Paused: card.stateIconGlyph = "pause_circle"; card.accentToken = "--md-sys-color-secondary"; break;
        case TransferState::Completed: card.stateIconGlyph = "check_circle"; card.accentToken = "--md-sys-color-primary"; break;
        case TransferState::Failed: card.stateIconGlyph = "error"; card.accentToken = "--md-sys-color-error"; break;
        case TransferState::Cancelled: card.stateIconGlyph = "cancel"; card.accentToken = "--md-sys-color-outline"; break;
    }
    card.supportingText = !item.statusMessage.empty()
                              ? item.statusMessage
                              : std::to_string(item.segmentCount) + " parallel segments";
    return card;
}

}  // namespace material_everything::download_manager
