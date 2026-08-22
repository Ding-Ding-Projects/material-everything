#include "material_everything/qt_multimedia_media_player_backend.hpp"

#include <QUrl>

namespace material_everything {

QtMultimediaMediaPlayerBackend::QtMultimediaMediaPlayerBackend(QObject* parent)
    : MediaPlayerBackend(parent) {
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                switch (state) {
                case QMediaPlayer::PlayingState:
                    publishState(State::Playing);
                    break;
                case QMediaPlayer::PausedState:
                    publishState(State::Paused);
                    break;
                case QMediaPlayer::StoppedState:
                    publishState(State::Stopped);
                    break;
                }
            });

    connect(&m_player, &QMediaPlayer::positionChanged, this,
            [this](qint64 positionMs) { emit positionChanged(std::chrono::milliseconds{positionMs}); });

    connect(&m_player, &QMediaPlayer::durationChanged, this,
            [this](qint64 durationMs) { emit durationChanged(std::chrono::milliseconds{durationMs}); });

    connect(&m_audioOutput, &QAudioOutput::volumeChanged, this,
            [this](float volume) { emit volumeChanged(volume); });

    connect(&m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) {
                emit failedToLoad(m_player.source(), message);
                publishState(State::Stopped);
            });
}

QtMultimediaMediaPlayerBackend::~QtMultimediaMediaPlayerBackend() = default;

QWidget* QtMultimediaMediaPlayerBackend::videoSurface() {
    m_videoWidget.setAspectRatioMode(Qt::KeepAspectRatio);
    m_videoWidget.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoWidget.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return &m_videoWidget;
}

void QtMultimediaMediaPlayerBackend::load(const QUrl& source) {
    if (!source.isValid() || source.isEmpty()) {
        emit failedToLoad(source, QStringLiteral("The media URL is empty or invalid."));
        return;
    }

    m_player.setSource(source);
    emit loaded(source.fileName());
}

void QtMultimediaMediaPlayerBackend::play() {
    if (m_player.source().isValid() && !m_player.source().isEmpty()) {
        m_player.play();
    }
}

void QtMultimediaMediaPlayerBackend::pause() { m_player.pause(); }

void QtMultimediaMediaPlayerBackend::stop() { m_player.stop(); }

std::chrono::milliseconds QtMultimediaMediaPlayerBackend::position() const {
    return std::chrono::milliseconds{m_player.position()};
}

std::chrono::milliseconds QtMultimediaMediaPlayerBackend::duration() const {
    return std::chrono::milliseconds{m_player.duration()};
}

void QtMultimediaMediaPlayerBackend::seek(std::chrono::milliseconds position) {
    if (position.count() < 0) position = std::chrono::milliseconds::zero();
    m_player.setPosition(static_cast<qint64>(position.count()));
}

float QtMultimediaMediaPlayerBackend::volume() const {
    return static_cast<float>(m_audioOutput.volume());
}

void QtMultimediaMediaPlayerBackend::setVolume(float normalizedVolume) {
    m_audioOutput.setVolume(qBound(0.0f, normalizedVolume, 1.0f));
}

} // namespace material_everything
