#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace me::screen_recorder {

enum class CaptureMode {
    Region,
    FullScreen,
    Window,
};

enum class OutputFormat {
    Mp4,
    WebM,
    Gif,
};

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct AudioSource {
    enum class Kind {
        Microphone,
        SystemLoopback,
        Application,
        None,
    };

    Kind kind = Kind::None;
    std::string deviceId;
    std::string displayName;
    double volume = 1.0;  // 0.0 through 1.0.
    bool muted = false;
};

struct RecorderSettings {
    CaptureMode mode = CaptureMode::FullScreen;
    std::uint32_t frameRate = 30;  // 5 through 120.
    OutputFormat format = OutputFormat::Mp4;
    Rect region;
    std::uintptr_t targetWindowHandle = 0;
    std::vector<AudioSource> audioSources;
    bool includeCursor = true;
    bool livePreviewEnabled = true;
};

enum class RecorderState {
    Ready,
    Recording,
    Paused,
    Stopping,
};

struct VideoFrame {
    Rect bounds;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bitsPerPixel = 32;
    std::vector<std::uint8_t> pixels;  // RGBA8888, row-major, top-down.
    std::chrono::system_clock::time_point capturedAt;
};

struct AudioChunk {
    std::string sourceId;
    std::uint32_t channels = 2;
    std::uint32_t sampleRate = 48000;
    std::uint16_t bitsPerSample = 16;
    std::vector<std::uint8_t> samples;  // Interleaved PCM.
    std::chrono::system_clock::time_point capturedAt;
};

struct RecordingResult {
    std::string outputPath;
    OutputFormat format = OutputFormat::Mp4;
    std::chrono::milliseconds duration{0};
    std::uint64_t videoBytes = 0;
    std::uint64_t audioBytes = 0;
    std::uint64_t frameCount = 0;
};

// Material Design 3 presentation metadata for the recorder controls.
struct RecorderTheme {
    std::string primaryColor = "#6750A4";       // MD3 primary.
    std::string surfaceColor = "#FFFBFE";       // MD3 surface.
    std::string errorColor = "#B3261E";         // MD3 error.
    std::string recordingLabel = "Recording";
    std::string pausedLabel = "Paused";
    std::string stopLabel = "Stop";
};

class ScreenRecorder {
public:
    using FrameCallback = std::function<void(const VideoFrame&)>;
    using StateCallback = std::function<void(RecorderState)>;
    using ErrorCallback = std::function<void(const std::string& message)>;

    explicit ScreenRecorder(std::string outputDirectory = {});

    void setSettings(const RecorderSettings& settings);
    const RecorderSettings& settings() const { return settings_; }
    RecorderState state() const { return state_; }

    void setFrameSink(FrameCallback sink);
    void setStateObserver(StateCallback observer);
    void setErrorHandler(ErrorCallback handler);

    bool start();
    bool pause();
    bool resume();
    bool stop();

    // Synchronous capture used by the preview pane and by tests that inject
    // a synthetic frame provider. The callback receives one RGBA frame.
    void requestPreviewFrame(FrameCallback sink) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RecorderSettings settings_;
    RecorderState state_ = RecorderState::Ready;
};

}  // namespace me::screen_recorder
