#include "material_everything/media_player_module.hpp"

#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>

namespace material_everything {
using namespace std::chrono_literals;

MediaPlayerModule::MediaPlayerModule(QWidget* parent)
    : QWidget(parent),
      m_backend(new QtMultimediaMediaPlayerBackend(this)),
      m_layout(new QVBoxLayout(this)),
      m_playlist(new QListWidget(this)),
      m_previousButton(nullptr),
      m_playPauseButton(nullptr),
      m_nextButton(nullptr),
      m_seekSlider(new QSlider(Qt::Horizontal, this)),
      m_volumeSlider(new QSlider(Qt::Horizontal, this)) {
    initializeUserInterface();

    connect(m_backend, &MediaPlayerBackend::failedToLoad, this,
            &MediaPlayerModule::onLoadFailed);
    connect(m_backend, &MediaPlayerBackend::positionChanged, this,
            &MediaPlayerModule::onPositionChanged);
    connect(m_backend, &MediaPlayerBackend::durationChanged, this,
            &MediaPlayerModule::onDurationChanged);
}

void MediaPlayerModule::initializeUserInterface() {
    setAccessibleName(QStringLiteral("Media Player"));
    setProperty("md3ContainerColor", QStringLiteral("surface"));
    setProperty("md3CornerRadius", 16);

    auto* videoHost = new QWidget(this);
    auto* videoLayout = new QVBoxLayout(videoHost);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoHost->setMinimumHeight(240);
    videoHost->setProperty("md3ContainerColor", QStringLiteral("surfaceContainerLowest"));
    videoHost->setProperty("md3CornerRadius", 12);
    videoLayout->addWidget(m_backend->videoSurface());

    m_playlist->setAccessibleName(QStringLiteral("Playlist"));
    m_playlist->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playlist->setUniformItemSizes(true);
    m_playlist->setProperty("md3Elevation", 1);
    m_playlist->setProperty("md3ShapeScale", 3);
    connect(m_playlist, &QListWidget::itemSelectionChanged, this,
            &MediaPlayerModule::onPlaylistSelectionChanged);

    m_seekSlider->setRange(0, 10000);
    m_seekSlider->setPageStep(250);
    m_seekSlider->setAccessibleName(QStringLiteral("Seek"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(70);
    m_volumeSlider->setAccessibleName(QStringLiteral("Volume"));
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this]() {
        onSeekRequested(m_seekSlider->value());
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            &MediaPlayerModule::onVolumeRequested);
    m_backend->setVolume(0.70F);

    m_previousButton =
        createMaterialButton(style()->standardIcon(QStyle::SP_MediaSkipBackward),
                             QStringLiteral("Previous track"));
    m_playPauseButton =
        createMaterialButton(style()->standardIcon(QStyle::SP_MediaPlay),
                             QStringLiteral("Play or pause"));
    m_nextButton = createMaterialButton(style()->standardIcon(QStyle::SP_MediaSkipForward),
                                        QStringLiteral("Next track"));
    connect(m_previousButton, &QPushButton::clicked, this,
            &MediaPlayerModule::previous);
    connect(m_playPauseButton, &QPushButton::clicked, this,
            &MediaPlayerModule::onPlayPauseClicked);
    connect(m_nextButton, &QPushButton::clicked, this, &MediaPlayerModule::next);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->addWidget(m_previousButton);
    controlsRow->addWidget(m_playPauseButton);
    controlsRow->addWidget(m_nextButton);
    controlsRow->addStretch(1);
    controlsRow->addWidget(m_seekSlider, 4);
    controlsRow->addWidget(m_volumeSlider, 2);

    m_layout->setContentsMargins(24, 24, 24, 24);
    m_layout->setSpacing(18);
    m_layout->addWidget(videoHost);
    m_layout->addWidget(m_playlist);
    m_layout->addLayout(controlsRow);
}

QPushButton*
MediaPlayerModule::createMaterialButton(const QIcon& icon, const QString& accessibleName) {
    auto* button = new QPushButton(icon, QString(), this);
    button->setAccessibleName(accessibleName);
    button->setIconSize(QSize(26, 26));
    button->setFixedSize(52, 52);
    button->setProperty("md3Variant", QStringLiteral("tonal"));
    button->setProperty("md3ShapeScale", -1);
    return button;
}

void MediaPlayerModule::addTrack(const MediaTrack& track) {
    if (track.source.isValid() && !track.source.isEmpty()) {
        m_tracks.push_back(track);
        auto* item = new QListWidgetItem(track.title, m_playlist);
        item->setData(Qt::UserRole, track.source);
        item->setToolTip(track.source.toString());
        emit playlistChanged();
    }
}

void MediaPlayerModule::addTracks(const QList<MediaTrack>& tracks) {
    for (const auto& track : tracks) {
        addTrack(track);
    }
}

void MediaPlayerModule::clearPlaylist() {
    m_tracks.clear();
    m_activeIndex.reset();
    m_playlist->clear();
    m_backend->stop();
    m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    emit playlistChanged();
}

void MediaPlayerModule::playTrack(int index) {
    if (index >= 0 && index < static_cast<int>(m_tracks.size())) {
        setCurrentIndex(index);
        m_backend->play();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    }
}

void MediaPlayerModule::next() {
    if (!m_tracks.empty()) {
        playTrack((currentIndex() + 1) % static_cast<int>(m_tracks.size()));
    }
}

void MediaPlayerModule::previous() {
    if (!m_tracks.empty()) {
        const int indexCount = static_cast<int>(m_tracks.size());
        playTrack((currentIndex() + indexCount - 1) % indexCount);
    }
}

void MediaPlayerModule::togglePlayback() {
    if (m_tracks.empty()) return;

    switch (m_backend->state()) {
    case MediaPlayerBackend::State::Playing:
        m_backend->pause();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        break;
    case MediaPlayerBackend::State::Paused:
        m_backend->play();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        break;
    case MediaPlayerBackend::State::Stopped:
        playTrack(currentIndex() < 0 ? 0 : currentIndex());
        break;
    }
}

int MediaPlayerModule::currentIndex() const { return m_activeIndex.value_or(-1); }

bool MediaPlayerModule::setCurrentIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_tracks.size())) return false;
    if (index == m_activeIndex.value_or(-1)) return true;

    m_activeIndex = index;
    m_playlist->blockSignals(true);
    m_playlist->setCurrentRow(index);
    m_playlist->blockSignals(false);
    m_backend->load(m_tracks[static_cast<std::size_t>(index)].source);
    emit activeTrackChanged(index, m_tracks[static_cast<std::size_t>(index)]);
    return true;
}

void MediaPlayerModule::onPlaylistSelectionChanged() {
    const auto selectedItems = m_playlist->selectedItems();
    if (selectedItems.empty()) return;
    setCurrentIndex(m_playlist->currentRow());
}

void MediaPlayerModule::onPlayPauseClicked() { togglePlayback(); }

void MediaPlayerModule::onSeekRequested(int value) {
    const auto durationMs = m_backend->duration().count();
    if (durationMs <= 0 || m_seekSlider->maximum() == 0) return;

    const auto target = durationMs * value / m_seekSlider->maximum();
    m_backend->seek(std::chrono::milliseconds{target});
}

void MediaPlayerModule::onPositionChanged(std::chrono::milliseconds position) {
    const auto durationMs = m_backend->duration().count();
    if (durationMs <= 0 || m_seekSlider->maximum() == 0) return;

    m_updatingSeekSlider = true;
    m_seekSlider->setValue(
        static_cast<int>(position.count() * m_seekSlider->maximum() / durationMs));
    m_updatingSeekSlider = false;
}

void MediaPlayerModule::onDurationChanged(std::chrono::milliseconds duration) {
    Q_UNUSED(duration);
    m_seekSlider->setValue(0);
}

void MediaPlayerModule::onVolumeRequested(int value) {
    m_backend->setVolume(value / 100.0F);
}

void MediaPlayerModule::onLoadFailed(const QUrl& source, const QString& reason) {
    const auto itemMatches = m_playlist->findItems(source.toString(), Qt::MatchExactly);
    if (!itemMatches.empty()) {
        itemMatches.first()->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    }
    setProperty("mediaPlayerLastError",
                QStringLiteral("%1: %2").arg(source.toString(), reason));
    update();
}

} // namespace material_everything
