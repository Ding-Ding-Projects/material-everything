#pragma once

#include "material_everything/media_player.hpp"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QSizePolicy>
#include <QVideoWidget>

namespace material_everything {

class QtMultimediaMediaPlayerBackend final : public MediaPlayerBackend {
public:
    explicit QtMultimediaMediaPlayerBackend(QObject* parent = nullptr);
    ~QtMultimediaMediaPlayerBackend() override;

    QWidget* videoSurface();

    void load(const QUrl& source) override;
    void play() override;
    void pause() override;
    void stop() override;
    std::chrono::milliseconds position() const override;
    std::chrono::milliseconds duration() const override;
    void seek(std::chrono::milliseconds position) override;
    float volume() const override;
    void setVolume(float normalizedVolume) override;

private:
    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QVideoWidget m_videoWidget;
};

} // namespace material_everything
