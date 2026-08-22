#include "torrent_client.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace me::torrent {
namespace {

std::int64_t NextId() {
  static std::int64_t counter = 1000;
  return ++counter;
}

}  // namespace

struct TorrentClient::Impl {
  struct Entry {
    TorrentInfo info;
    std::string source;  // file path or magnet URI
  };

  mutable std::mutex mu;
  std::unordered_map<std::int64_t, Entry> torrents;
  std::deque<SpeedSample> samples;

  void PushSample(double down, double up) {
    samples.push_back({down, up});
    while (samples.size() > 240) samples.pop_front();  // ~4 min at 1 Hz
  }
};

TorrentClient::TorrentClient() : impl_(std::make_unique<Impl>()) {}
TorrentClient::~TorrentClient() = default;

void TorrentClient::ApplySettings(const TorrentSettings& s) {
  settings_ = s;
  std::lock_guard<std::mutex> lock(impl_->mu);
  // Backend hook: forward listen_port / rate limits into the live
  // libtorrent session here once the real session wiring lands.
}

std::int64_t TorrentClient::AddTorrentFile(const std::string& path) {
  std::int64_t id = NextId();
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto name = path.substr(path.find_last_of("/\\") + 1);
  impl_->torrents[id] = Impl::Entry{{id, name, TorrentState::kDownloading}, path};
  return id;
}

std::int64_t TorrentClient::AddMagnet(const std::string& magnet_uri) {
  std::int64_t id = NextId();
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto label = magnet_uri.substr(0, std::min<size_t>(48, magnet_uri.size()));
  impl_->torrents[id] = Impl::Entry{{id, label, TorrentState::kQueued}, magnet_uri};
  return id;
}

void TorrentClient::Pause(std::int64_t id) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (auto* e = &impl_->torrents[id]; e->info.id == id)
    e->info.state = TorrentState::kPaused;
}

void TorrentClient::Resume(std::int64_t id) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (auto* e = &impl_->torrents[id]; e->info.id == id)
    e->info.state = TorrentState::kDownloading;
}

void TorrentClient::Remove(std::int64_t id, bool /*keep_files*/) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->torrents.erase(id);
}

void TorrentClient::SetSequential(std::int64_t id, bool sequential) {
  std::lock_guard<std::mutex> lock(impl_->mu);
  if (auto it = impl_->torrents.find(id); it != impl_->torrents.end())
    it->second.info.sequential = sequential;
}

std::vector<TorrentInfo> TorrentClient::ListTorrents() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  std::vector<TorrentInfo> out;
  out.reserve(impl_->torrents.size());
  for (auto& [_, e] : impl_->torrents) out.push_back(e.info);
  return out;
}

std::vector<SpeedSample> TorrentClient::RecentSpeedSamples() const {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return {impl_->samples.begin(), impl_->samples.end()};
}

}  // namespace me::torrent
