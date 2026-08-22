#include "screen_recorder.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace me::screen_recorder {
namespace {

std::string formatExtension(OutputFormat format) {
    switch (format) {
        case OutputFormat::Mp4:
            return "mp4";
        case OutputFormat::WebM:
            return "webm";
        case OutputFormat::Gif:
            return "gif";
    }
    return "mp4";
}

bool validRect(const Rect& rect) {
    return rect.width > 0 && rect.height > 0;
}

}  // namespace

struct ScreenRecorder::Impl {
    std::string outputDirectory;
    FrameCallback frameSink;
    StateCallback stateObserver;
    ErrorCallback errorHandler;
    std::atomic_bool active{false};
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::milliseconds recordedTime{0};
    std::uint64_t framesWritten = 0;

    void reportError(const std::string& message) const {
        if (errorHandler) errorHandler(message);
    }
};

ScreenRecorder::ScreenRecorder(std::string outputDirectory)
    : impl_(std::make_unique<Impl>()) {
    impl_->outputDirectory = std::move(outputDirectory);
}

void ScreenRecorder::setSettings(const RecorderSettings& settings) {
    if (state_ == RecorderState::Recording) {
        if (impl_->errorHandler) {
            impl_->errorHandler("Cannot change settings while recording");
        }
        return;
    }
    if (!validRect(settings.region)) {
        if (impl_->errorHandler) impl_->errorHandler("Region capture requires a positive rectangle");
        return;
    }
    if (settings.frameRate < 5 || settings.frameRate > 120) {
        if (impl_->errorHandler) impl_->errorHandler("Frame rate must be between 5 and 120");
        return;
    }
    for (const auto& audio : settings.audioSources) {
        if (audio.volume < 0.0 || audio.volume > 1.0) {
            if (impl_->errorHandler) impl_->errorHandler("Audio volume must be between 0.0 and 1.0");
            return;
        }
    }
    settings_ = settings;
}

void ScreenRecorder::setFrameSink(FrameCallback sink) { impl_->frameSink = std::move(sink); }
void ScreenRecorder::setStateObserver(StateCallback observer) { impl_->stateObserver = std::move(observer); }
void ScreenRecorder::setErrorHandler(ErrorCallback handler) { impl_->errorHandler = std::move(handler); }

bool ScreenRecorder::start() {
    if (state_ == RecorderState::Recording) return false;
    state_ = RecorderState::Recording;
    impl_->active.store(true);
    impl_->startedAt = std::chrono::steady_clock::now();
    impl_->framesWritten = 0;
    if (impl_->stateObserver) impl_->stateObserver(state_);
    return true;
}

bool ScreenRecorder::pause() {
    if (state_ != RecorderState::Recording) return false;
    const auto now = std::chrono::steady_clock::now();
    impl_->recordedTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->startedAt);
    state_ = RecorderState::Paused;
    if (impl_->stateObserver) impl_->stateObserver(state_);
    return true;
}

bool ScreenRecorder::resume() {
    if (state_ != RecorderState::Paused) return false;
    impl_->startedAt = std::chrono::steady_clock::now();
    state_ = RecorderState::Recording;
    if (impl_->stateObserver) impl_->stateObserver(state_);
    return true;
}

bool ScreenRecorder::stop() {
    if (state_ != RecorderState::Recording && state_ != RecorderState::Paused) return false;
    state_ = RecorderState::Stopping;
    if (impl_->stateObserver) impl_->stateObserver(state_);
    impl_->active.store(false);
    state_ = RecorderState::Ready;
    impl_->recordedTime = {};
    if (impl_->stateObserver) impl_->stateObserver(state_);
    return true;
}

void ScreenRecorder::requestPreviewFrame(FrameCallback sink) const {
    if (!sink) return;
    Rect bounds = settings_.region;
    if (settings_.mode == CaptureMode::FullScreen && !validRect(bounds)) {
        bounds = Rect{0, 0, 1280, 720};  // Host display service supplies the real bounds.
    }
    VideoFrame frame;
    frame.bounds = bounds;
    frame.width = static_cast<std::uint32_t>(bounds.width);
    frame.height = static_cast<std::uint32_t>(bounds.height);
    frame.capturedAt = std::chrono::system_clock::now();
    frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height * 4u, 0u);
    sink(frame);
}

}  // namespace me::screen_recorder
