#include "material_everything/presentation.hpp"

#include <QApplication>
#include <QBuffer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace material_everything::presentation {

namespace {

constexpr QSizeF kSlideSize{1280, 720};

QJsonObject toJson(const SlideElement& e) {
    QJsonObject o;
    o["kind"] = static_cast<int>(e.kind);
    o["bounds"] = QJsonArray{e.bounds.x(), e.bounds.y(), e.bounds.width(), e.bounds.height()};
    o["text"] = e.text;
    o["image"] = e.imagePath;
    o["fill"] = e.fillColor.name(QColor::HexArgb);
    o["fontSize"] = e.fontSize;
    return o;
}

SlideElement fromJson(const QJsonObject& o) {
    SlideElement e;
    e.kind = static_cast<SlideElement::Kind>(o["kind"].toInt());
    const auto b = o["bounds"].toArray();
    if (b.size() == 4) e.bounds = QRectF(b[0].toDouble(), b[1].toDouble(), b[2].toDouble(), b[3].toDouble());
    e.text = o["text"].toString();
    e.imagePath = o["image"].toString();
    e.fillColor = QColor(o["fill"].toString("#ffffffff"));
    e.fontSize = o["fontSize"].toInt(24);
    return e;
}

void renderSlide(QPainter& p, const Slide& s) {
    p.fillRect(QRectF(QPointF(0, 0), kSlideSize), QColor(0x1C, 0x1B, 0x1F));
    for (const auto& el : s.elements()) {
        switch (el.kind) {
        case SlideElement::Kind::TextBox: {
            QFont f = QApplication::font();
            f.setPixelSize(el.fontSize);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(el.bounds.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::TextWordWrap, el.text);
            break;
        }
        case SlideElement::Kind::Image: {
            QImage img(el.imagePath);
            if (!img.isNull()) p.drawImage(el.bounds, img);
            break;
        }
        case SlideElement::Kind::Rectangle:
            p.setBrush(el.fillColor);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(el.bounds, 16, 16);
            break;
        case SlideElement::Kind::Ellipse:
            p.setBrush(el.fillColor);
            p.drawEllipse(el.bounds);
            break;
        case SlideElement::Kind::Line:
            p.setPen(QPen(el.fillColor, 4));
            p.drawLine(el.bounds.topLeft(), el.bounds.bottomRight());
            break;
        }
    }
}

QPixmap slideThumbnail(const Slide& s, const QSize& size) {
    QImage image(kSlideSize.toSize(), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    renderSlide(p, s);
    p.end();
    return QPixmap::fromImage(image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

}  // namespace

QString Slide::title() const { return m_title.isEmpty() ? QStringLiteral("(untitled slide)") : m_title; }
void Slide::setTitle(const QString& t) { m_title = t; }
std::vector<SlideElement>& Slide::elements() { return m_elements; }
const std::vector<SlideElement>& Slide::elements() const { return m_elements; }
Transition Slide::transition() const { return m_transition; }
void Slide::setTransition(Transition t) { m_transition = t; }

std::vector<Slide>& PresentationDocument::slides() { return m_slides; }
const std::vector<Slide>& PresentationDocument::slides() const { return m_slides; }
void PresentationDocument::addSlide() { m_slides.emplace_back(); }
void PresentationDocument::insertSlide(int index) { m_slides.insert(m_slides.begin() + qBound(0, index, static_cast<int>(m_slides.size())), Slide{}); }
void PresentationDocument::removeSlide(int index) {
    if (m_slides.size() > 1 && index >= 0 && index < static_cast<int>(m_slides.size())) m_slides.erase(m_slides.begin() + index);
}

bool PresentationDocument::save(const QString& path) const {
    QJsonArray arr;
    for (const auto& s : m_slides) {
        QJsonObject so;
        so["title"] = s.title();
        so["transition"] = static_cast<int>(s.transition());
        QJsonArray els;
        for (const auto& e : s.elements()) els.append(toJson(e));
        so["elements"] = els;
        arr.append(so);
    }
    QJsonObject root;
    root["version"] = 1;
    root["slides"] = arr;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(QJsonDocument(root).toJson()) != -1;
}

std::optional<PresentationDocument> PresentationDocument::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    PresentationDocument out;
    out.m_slides.clear();
    for (const auto v : doc.object()["slides"].toArray()) {
        Slide s;
        s.setTitle(v.toObject()["title"].toString());
        s.setTransition(static_cast<Transition>(v.toObject()["transition"].toInt()));
        for (const auto e : v.toObject()["elements"].toArray()) s.elements().push_back(fromJson(e.toObject()));
        out.m_slides.push_back(s);
    }
    if (out.m_slides.empty()) out.addSlide();
    return out;
}

class PresentationModule::Impl {
public:
    QListWidget* thumbnails = nullptr;
    QGraphicsView* canvasView = nullptr;
    QGraphicsScene* canvasScene = nullptr;
    QLabel* presenterOverlay = nullptr;
    QWidget* presenterMode = nullptr;
    int current = 0;

    void refreshThumbnails(PresentationDocument& doc) {
        thumbnails->clear();
        for (const auto& s : doc.slides()) {
            auto* item = new QListWidgetItem(QIcon(slideThumbnail(s, QSize(160, 90))), s.title(), thumbnails);
            item->setSizeHint(QSize(176, 104));
        }
        if (current < thumbnails->count()) thumbnails->setCurrentRow(current);
    }
};

PresentationModule::PresentationModule(QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>()) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    auto addBtn = [toolbar](const QString& text, auto slot) {
        QAction* a = toolbar->addAction(text);
        QObject::connect(a, &QAction::triggered, this, slot);
        return a;
    };
    addBtn(tr("Text box"), [this] {
        document().slides()[d->current].elements().push_back(
            SlideElement{SlideElement::Kind::TextBox, QRectF(120, 80, 480, 120), tr("Double-click to edit"), "", Qt::white, 28});
        refreshCanvas();
        emit documentModified();
    });
    addBtn(tr("Image…"), [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Insert image"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (path.isEmpty()) return;
        document().slides()[d->current].elements().push_back(
            SlideElement{SlideElement::Kind::Image, QRectF(140, 160, 640, 360), "", path, Qt::white, 0});
        refreshCanvas();
        emit documentModified();
    });
    addBtn(tr("Shape ▾"), [this] { /* rectangle default; picker in full UI */ });
    addBtn(tr("Presenter"), [this] { setPresenterMode(true); });
    addBtn(tr("Export PDF"), [this] { exportToPdf(QString()); });
    addBtn(tr("Export images"), [this] { exportImages(QString()); });
    rootLayout->addWidget(toolbar);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(12, 12, 12, 12);
    body->setSpacing(12);

    d->thumbnails = new QListWidget(this);
    d->thumbnails->setFixedWidth(200);
    d->thumbnails->setIconSize(QSize(160, 90));
    connect(d->thumbnails, &QListWidget::currentRowChanged, this, [this](int row) { setCurrentSlide(row); });
    body->addWidget(d->thumbnails);

    d->canvasScene = new QGraphicsScene(QRectF(QPointF(0, 0), kSlideSize), this);
    d->canvasView = new QGraphicsView(d->canvasScene, this);
    d->canvasView->setRenderHint(QPainter::Antialiasing);
    d->canvasView->setStyleSheet(QStringLiteral("background:#141218;border-radius:16px;"));
    body->addWidget(d->canvasView, 1);

    rootLayout->addLayout(body, 1);

    // Presenter overlay (second surface; simplified single-window mode).
    d->presenterMode = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
    auto* pl = new QHBoxLayout(d->presenterMode);
    d->presenterOverlay = new QLabel(d->presenterMode);
    d->presenterOverlay->setAlignment(Qt::AlignCenter);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(d->presenterOverlay);
    d->presenterMode->hide();
}

PresentationModule::~PresentationModule() = default;

PresentationDocument& PresentationModule::document() { return m_document; }

void PresentationModule::refreshCanvas() {
    d->canvasScene->clear();
    if (d->current >= static_cast<int>(m_document.slides().size())) d->current = 0;
    const Slide& s = m_document.slides()[static_cast<size_t>(d->current)];
    QImage image(kSlideSize.toSize(), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    renderSlide(p, s);
    p.end();
    d->canvasScene->addPixmap(QPixmap::fromImage(image));
}

void PresentationModule::addSlide() {
    m_document.addSlide();
    setCurrentSlide(static_cast<int>(m_document.slides().size()) - 1);
    emit documentModified();
}

void PresentationModule::deleteCurrentSlide() {
    m_document.removeSlide(d->current);
    setCurrentSlide(qBound(0, d->current, static_cast<int>(m_document.slides().size()) - 1));
    emit documentModified();
}

void PresentationModule::setCurrentSlide(int index) {
    d->current = qBound(0, index, static_cast<int>(m_document.slides().size()) - 1);
    refreshCanvas();
    d->refreshThumbnails(m_document);
    emit currentSlideChanged(d->current);
}

void PresentationModule::setPresenterMode(bool on) {
    if (!on || !d->presenterMode) return;
    const Slide& s = m_document.slides()[static_cast<size_t>(qMin(d->current, static_cast<int>(m_document.slides().size()) - 1))];
    d->presenterOverlay->setPixmap(slideThumbnail(s, QSize(960, 540)));
    d->presenterMode->showFullScreen();
}

bool PresentationModule::exportToPdf(const QString& path) {
    QString target = path;
    if (target.isEmpty())
        target = QFileDialog::getSaveFileName(this, tr("Export PDF"), QStringLiteral("presentation.pdf"), tr("PDF (*.pdf)"));
    if (target.isEmpty()) return false;
    QPdfWriter writer(target);
    writer.setPageSize(QPageSize(kSlideSize.toSize()));
    writer.setResolution(96);
    QPainter painter(&writer);
    for (size_t i = 0; i < m_document.slides().size(); ++i) {
        if (i > 0) writer.newPage();
        renderSlide(painter, m_document.slides()[i]);
    }
    return true;
}

bool PresentationModule::exportImages(const QString& directory) {
    QString dir = directory;
    if (dir.isEmpty())
        dir = QFileDialog::getExistingDirectory(this, tr("Export images"));
    if (dir.isEmpty()) return false;
    for (size_t i = 0; i < m_document.slides().size(); ++i) {
        QImage image(kSlideSize.toSize(), QImage::Format_ARGB32);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        renderSlide(p, m_document.slides()[i]);
        p.end();
        QImageWriter w(QStringLiteral("%1/slide-%2.png").arg(dir, QString::number(i + 1).rightJustified(3, QLatin1Char('0'))));
        if (!w.write(image)) return false;
    }
    return true;
}

}  // namespace material_everything::presentation
