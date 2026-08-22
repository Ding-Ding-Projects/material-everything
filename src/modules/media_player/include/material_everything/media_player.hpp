#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QWidget>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace material_everything {

struct MediaTrack final {
    QUrl source;
    QString title;
};

class MediaPlayerBackend : public QObject {
    Q_OBJECT

public:
    enum class State { Stopped, Playing, Paused };

    using QObject::QObject;
    virtual ~MediaPlayerBackend() = default;

    virtual void load(const QUrl& source) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual std::chrono::milliseconds position() const = 0;
    virtual std::chrono::milliseconds duration() const = 0;
    virtual void seek(std::chrono::milliseconds position) = 0;
    virtual float volume() const = 0;
    virtual void setVolume(float normalizedVolume) = 0;
    [[nodiscard]] State state() const noexcept { return m_state; }

signals:
    void loaded(const QString& title);
    void playbackStateChanged(MediaPlayerBackend::State state);
    void positionChanged(std::chrono::milliseconds position);
    void durationChanged(std::chrono::milliseconds duration);
    void volumeChanged(float normalizedVolume);
    void failedToLoad(const QUrl& source, const QString& reason);

protected:
    void publishState(State state) {
        if (state == m_state) return;
        m_state = state;
        emit playbackStateChanged(state);
    }

private:
    State m_state = State::Stopped;
};

} // namespace material_everything

Q_DECLARE_METATYPE(material_everything::MediaPlayerBackend::State)
