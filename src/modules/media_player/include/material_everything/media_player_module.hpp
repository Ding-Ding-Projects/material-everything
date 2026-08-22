#pragma once

#include "material_everything/media_player.hpp"
#include "material_everything/qt_multimedia_media_player_backend.hpp"

#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

namespace material_everything {

class MediaPlayerModule final : public QWidget {
    Q_OBJECT

public:
    explicit MediaPlayerModule(QWidget* parent = nullptr);

    void addTrack(const MediaTrack& track);
    void addTracks(const QList<MediaTrack>& tracks);
    void clearPlaylist();
    void playTrack(int index);
    void next();
    void previous();
    void togglePlayback();

signals:
    void activeTrackChanged(int index, const MediaTrack& track);
    void playlistChanged();

private slots:
    void onPlaylistSelectionChanged();
    void onPlayPauseClicked();
    void onSeekRequested(int value);
    void onPositionChanged(std::chrono::milliseconds position);
    void onDurationChanged(std::chrono::milliseconds duration);
    void onVolumeRequested(int value);
    void onLoadFailed(const QUrl& source, const QString& reason);

private:
    void initializeUserInterface();
    QPushButton* createMaterialButton(const QIcon& icon, const QString& accessibleName);
    int currentIndex() const;
    bool setCurrentIndex(int index);

    QtMultimediaMediaPlayerBackend* m_backend;
    QVBoxLayout* m_layout;
    QListWidget* m_playlist;
    QPushButton* m_previousButton;
    QPushButton* m_playPauseButton;
    QPushButton* m_nextButton;
    QSlider* m_seekSlider;
    QSlider* m_volumeSlider;
    std::vector<MediaTrack> m_tracks;
    std::optional<int> m_activeIndex;
    bool m_updatingSeekSlider = false;
};

} // namespace material_everything
