#pragma once
// Material Everything — Torrent Client module (public API).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace me::torrent {

struct TorrentSettings {
  int listen_port = 6881;
  std::string download_directory;
  int max_download_kbps = 0;  // 0 = unlimited
  int max_upload_kbps = 0;
};

enum class TorrentState { kQueued, kDownloading, kSeeding, kPaused, kDone, kError };

struct PeerInfo {
  std::string address;
  std::string client_name;
  int down_kbps = 0;
  int up_kbps = 0;
};

struct TorrentInfo {
  std::int64_t id = 0;
  std::string name;
  TorrentState state = TorrentState::kQueued;
  double progress = 0.0;          // 0..1
  double down_kbps = 0.0;
  double up_kbps = 0.0;
  bool sequential = false;
  std::vector<PeerInfo> peers;
};

// Speed-sample ring used by the UI graph.
struct SpeedSample {
  double down_kbps = 0.0;
  double up_kbps = 0.0;
};

class ITorrentBackend {
 public:
  virtual ~ITorrentBackend() = default;
  virtual void Start() = 0;
  virtual void Stop() = 0;
};

// Facade over a libtorrent-backed engine. The concrete libtorrent session
// is owned by the implementation; this header stays dependency-free so the
// module can be consumed by the shell without linking libtorrent headers.
class TorrentClient {
 public:
  TorrentClient();
  ~TorrentClient();

  TorrentClient(const TorrentClient&) = delete;
  TorrentClient& operator=(const TorrentClient&) = delete;

  void ApplySettings(const TorrentSettings& settings);
  const TorrentSettings& settings() const { return settings_; }

  // Returns torrent id.
  std::int64_t AddTorrentFile(const std::string& path);
  std::int64_t AddMagnet(const std::string& magnet_uri);
  void Pause(std::int64_t id);
  void Resume(std::int64_t id);
  void Remove(std::int64_t id, bool keep_files = true);
  void SetSequential(std::int64_t id, bool sequential);

  std::vector<TorrentInfo> ListTorrents() const;
  std::vector<SpeedSample> RecentSpeedSamples() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  TorrentSettings settings_;
};

}  // namespace me::torrent
