#include "paint.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QUndoCommand>
#include <algorithm>
#include <limits>

namespace material_everything::paint {
namespace {

constexpr QSize kDefaultCanvasSize{960, 600};

class CanvasCommand final : public QUndoCommand {
public:
    CanvasCommand(PaintModule* module, int layerIndex, QImage before, QImage after,
                  const QString& label)
        : module_(module), layerIndex_(layerIndex), before_(std::move(before)),
          after_(std::move(after)) {
        setText(label);
    }

    void undo() override {
        if (auto* layer = module_->layerAt(layerIndex_); layer != nullptr) {
            layer->image = before_;
            emit module_->canvasChanged();
        }
    }

    void redo() override {
        if (auto* layer = module_->layerAt(layerIndex_); layer != nullptr) {
            layer->image = after_;
            emit module_->canvasChanged();
        }
    }

private:
    PaintModule* module_;
    int layerIndex_;
    QImage before_;
    QImage after_;
};

class ResizeCommand final : public QUndoCommand {
public:
    ResizeCommand(PaintModule* module, std::vector<Layer> next, int selectedLayer)
        : module_(module), next_(std::move(next)), selectedLayer_(selectedLayer) {
        setText(PaintModule::tr("Resize canvas"));
    }

    void undo() override {
        module_->restoreLayers(previous_, selectedLayer_);
    }

    void redo() override {
        if (previous_.empty()) {
            previous_ = module_->takeLayersSnapshot();
        }
        module_->installLayers(next_, selectedLayer_);
    }

private:
    PaintModule* module_;
    std::vector<Layer> previous_;
    std::vector<Layer> next_;
    int selectedLayer_;
};

class RemoveLayerCommand final : public QUndoCommand {
public:
    RemoveLayerCommand(PaintModule* module, int removedIndex, const std::vector<Layer>& layers)
        : module_(module), removedIndex_(removedIndex), removed_(layers[static_cast<std::size_t>(removedIndex)]) {
        auto copy = layers;
        copy.erase(copy.begin() + removedIndex_);
        after_ = std::move(copy);
        setText(module->tr("Remove layer"));
    }

    void undo() override {
        module_->insertLayer(removedIndex_, removed_);
    }

    void redo() override {
        module_->eraseLayerOnly(removedIndex_, static_cast<int>(after_.size()));
    }

private:
    PaintModule* module_;
    int removedIndex_;
    Layer removed_;
    std::vector<Layer> after_;
};

bool validPercent(int value)
{
    return value >= 0 && value <= 100;
}

}  // namespace

PaintModule::PaintModule(QWidget* parent) : QWidget(parent)
{
    newCanvas(kDefaultCanvasSize.width(), kDefaultCanvasSize.height());
}

QString PaintModule::toolLabel(Tool tool)
{
    switch (tool) {
    case Tool::Brush: return QObject::tr("Brush");
    case Tool::Pencil: return QObject::tr("Pencil");
    case Tool::Eraser: return QObject::tr("Eraser");
    case Tool::Fill: return QObject::tr("Fill");
    case Tool::Line: return QObject::tr("Line");
    case Tool::Rectangle: return QObject::tr("Rectangle");
    case Tool::Ellipse: return QObject::tr("Ellipse");
    case Tool::Text: return QObject::tr("Text");
    }
    return {};
}

bool PaintModule::newCanvas(int width, int height)
{
    if (width < kMinimumCanvasSize || height < kMinimumCanvasSize ||
        width > 16384 || height > 16384) {
        emit statusMessage(tr("Canvas size must be between 1 and 16384 pixels."), true);
        return false;
    }

    undoStack_.clear();
    layers_.clear();
    layers_.push_back({tr("Background"), QImage(width, height, QImage::Format_ARGB32_Premultiplied),
                        true, 100});
    layers_.front().image.fill(Qt::white);
    activeLayer_ = 0;
    emit layersChanged();
    emit canvasChanged();
    updateGeometry();
    update();
    return true;
}

bool PaintModule::resizeCanvas(int width, int height, bool scaleContents)
{
    if (layers_.empty() || width < kMinimumCanvasSize || height < kMinimumCanvasSize ||
        width > 16384 || height > 16384) {
        emit statusMessage(tr("Resize requires a valid canvas and a size from 1 to 16384 pixels."),
                           true);
        return false;
    }

    const auto oldSize = QSize(layers_.front().image.width(), layers_.front().image.height());
    if (oldSize == QSize(width, height)) {
        return true;
    }

    std::vector<Layer> next;
    next.reserve(layers_.size());
    for (const Layer& source : layers_) {
        Layer resized = source;
        QImage target(width, height, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::transparent);
        if (scaleContents && !source.image.isNull()) {
            resized.image = source.image.scaled(width, height, Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation);
        } else if (!source.image.isNull()) {
            QPainter painter(&target);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(0, 0, source.image);
        }
        resized.image = scaleContents ? resized.image : target;
        next.push_back(std::move(resized));
    }

    const int selected = std::clamp(activeLayer_, 0, static_cast<int>(next.size()) - 1);
    undoStack_.push(new ResizeCommand(this, std::move(next), selected));
    return true;
}

void PaintModule::setTool(Tool tool)
{
    currentTool_ = tool;
    setCursor(Qt::CrossCursor);
}

void PaintModule::setColor(const QColor& color)
{
    if (!color.isValid()) {
        emit statusMessage(tr("Choose a valid colour."), true);
        return;
    }
    currentColor_ = color;
}

void PaintModule::setBrushSize(int size)
{
    brushSize_ = std::clamp(size, 1, kMaximumBrushSize);
}

void PaintModule::setLayerOpacity(int layerIndex, int percent)
{
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size()) ||
        !validPercent(percent)) {
        return;
    }
    layers_[static_cast<std::size_t>(layerIndex)].opacity = percent;
    emit canvasChanged();
    update();
}

void PaintModule::setLayerVisible(int layerIndex, bool visible)
{
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) {
        return;
    }
    layers_[static_cast<std::size_t>(layerIndex)].visible = visible;
    emit layersChanged();
    update();
}

void PaintModule::addLayer()
{
    if (layers_.size() >= std::numeric_limits<quint16>::max()) {
        emit statusMessage(tr("Too many layers."), true);
        return;
    }
    const QSize size = layers_.empty()
        ? kDefaultCanvasSize
        : layers_.front().image.size();
    Layer layer;
    layer.name = tr("Layer %1").arg(layers_.size() + 1);
    layer.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    layer.image.fill(Qt::transparent);
    layers_.push_back(std::move(layer));
    activeLayer_ = static_cast<int>(layers_.size()) - 1;
    emit layersChanged();
    emit canvasChanged();
    update();
}

void PaintModule::removeLayer(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size()) || layers_.size() == 1U) {
        emit statusMessage(tr("At least one layer is required."), true);
        return;
    }
    undoStack_.push(new RemoveLayerCommand(this, layerIndex, layers_));
    activeLayer_ = std::clamp(activeLayer_ - (activeLayer_ >= layerIndex ? 1 : 0), 0,
                              static_cast<int>(layers_.size()) - 1);
    if (layers_.empty()) {
        addLayer();
    }
}

void PaintModule::selectLayer(int layerIndex)
{
    if (layerIndex >= 0 && layerIndex < static_cast<int>(layers_.size())) {
        activeLayer_ = layerIndex;
        emit layersChanged();
    }
}

QImage PaintModule::flattenedImage() const
{
    if (layers_.empty()) {
        return {};
    }
    QImage output(layers_.front().image.size(), QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::white);
    renderLayers(output);
    return output;
}

void PaintModule::renderLayers(QImage& destination) const
{
    QPainter painter(&destination);
    for (const Layer& layer : layers_) {
        if (layer.visible && !layer.image.isNull()) {
            painter.setOpacity(static_cast<qreal>(layer.opacity) / 100.0);
            painter.drawImage(0, 0, layer.image);
        }
    }
}

std::optional<QImage> PaintModule::exportImage(const QString& path) const
{
    if (path.isEmpty() || layers_.empty()) {
        emit statusMessage(tr("Choose a destination file first."), true);
        return std::nullopt;
    }

    QImage image = flattenedImage();
    if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        image.save(path, "PNG");
    } else {
        // JPG cannot preserve alpha; flatten against white explicitly.
        QImage opaque(image.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        if (!opaque.save(path, "JPEG", 95)) {
            emit statusMessage(tr("Could not write %1.").arg(path), true);
            return std::nullopt;
        }
    }
    emit statusMessage(tr("Exported %1.").arg(path), false);
    return image;
}

Layer* PaintModule::layerAt(int index)
{
    if (index < 0 || index >= static_cast<int>(layers_.size())) {
        return nullptr;
    }
    return &layers_[static_cast<std::size_t>(index)];
}

Layer* PaintModule::mutableLayer()
{
    return layerAt(activeLayer_);
}

std::vector<Layer> PaintModule::takeLayersSnapshot()
{
    return layers_;
}

void PaintModule::installLayers(std::vector<Layer> next, int selected)
{
    layers_ = std::move(next);
    activeLayer_ = std::clamp(selected, 0, static_cast<int>(layers_.size()) - 1);
    emit layersChanged();
    emit canvasChanged();
    update();
}

void PaintModule::restoreLayers(const std::vector<Layer>& previous, int selected)
{
    installLayers(previous, selected);
}

void PaintModule::insertLayer(int index, const Layer& layer)
{
    index = std::clamp(index, 0, static_cast<int>(layers_.size()));
    layers_.insert(layers_.begin() + index, layer);
    activeLayer_ = index;
    emit layersChanged();
    emit canvasChanged();
    update();
}

void PaintModule::eraseLayerOnly(int index, int minimumCount)
{
    if (index < 0 || index >= static_cast<int>(layers_.size()) ||
        static_cast<int>(layers_.size()) <= std::max(minimumCount, 1)) {
        return;
    }
    layers_.erase(layers_.begin() + index);
    activeLayer_ = std::clamp(activeLayer_, 0, static_cast<int>(layers_.size()) - 1);
    emit layersChanged();
    emit canvasChanged();
    update();
}

void PaintModule::paintAt(const QPoint& current, const QPoint& previous)
{
    auto* layer = mutableLayer();
    if (layer == nullptr || layer->image.isNull()) {
        return;
    }

    QPainter painter(&layer->image);
    painter.setRenderHint(QPainter::Antialiasing, currentTool_ == Tool::Brush);
    QColor paintColor = currentColor_;
    qreal width = static_cast<qreal>(brushSize_);
    if (currentTool_ == Tool::Eraser) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        paintColor = Qt::black;
    } else if (currentTool_ == Tool::Pencil) {
        width = 1.0;
    }

    QPen pen(paintColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(current == previous ? QRect(current, current).center() : previous, current);

    // Keep a live raster snapshot for smooth freehand preview without mutating history.
    stroke_.preview = flattenedImage();
    stroke_.preview.detach();
}

void PaintModule::floodFill(const QPoint& origin)
{
    auto* layer = mutableLayer();
    if (layer == nullptr || !layer->image.rect().contains(origin)) {
        return;
    }

    QImage& image = layer->image;
    const QRgb targetColor = image.pixel(origin);
    QColor replacement = currentColor_;
    if (currentTool_ == Tool::Eraser) {
        replacement = Qt::transparent;
    }
    const QRgb newColor = replacement.rgba();
    if (targetColor == newColor || qAlpha(targetColor) == 255 && qAlpha(newColor) == 255 &&
                                       QColor(targetColor) == replacement) {
        return;
    }

    struct Visit { int x; int y; };
    std::vector<Visit> pending;
    pending.push_back({origin.x(), origin.y()});
    while (!pending.empty()) {
        const Visit visit = pending.back();
        pending.pop_back();
        if (!image.rect().contains(visit.x, visit.y) || image.pixel(visit.x, visit.y) != targetColor) {
            continue;
        }
        int left = visit.x;
        while (left > 0 && image.pixel(left - 1, visit.y) == targetColor) {
            --left;
        }
        int right = visit.x;
        while (right + 1 < image.width() && image.pixel(right + 1, visit.y) == targetColor) {
            ++right;
        }
        for (int x = left; x <= right; ++x) {
            image.setPixel(x, visit.y, newColor);
            if (visit.y > 0 && image.pixel(x, visit.y - 1) == targetColor) {
                pending.push_back({x, visit.y - 1});
            }
            if (visit.y + 1 < image.height() && image.pixel(x, visit.y + 1) == targetColor) {
                pending.push_back({x, visit.y + 1});
            }
        }
    }
}

void PaintModule::applyTextToLayer()
{
    auto* layer = mutableLayer();
    if (layer == nullptr || pendingText_.isEmpty()) {
        return;
    }
    QPainter painter(&layer->image);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path = textPathFor(stroke_.start);
    painter.fillPath(path, currentTool_ == Tool::Eraser ? QColor(Qt::white) : currentColor_);
    stroke_.preview = QImage();
    update();
}

QPainterPath PaintModule::textPathFor(const QPoint& position) const
{
    QFont font = QApplication::font();
    font.setPixelSize(std::max(14, brushSize_ * 2));
    QPainterPath path;
    path.addText(position, font, pendingText_);
    return path;
}

void PaintModule::drawPreview(QPainter& painter) const
{
    painter.save();
    QPen pen(currentColor_, static_cast<qreal>(brushSize_), Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    if (currentTool_ == Tool::Eraser) {
        pen.setColor(Qt::white);
    }
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    switch (currentTool_) {
    case Tool::Line:
        painter.drawLine(stroke_.start, stroke_.last);
        break;
    case Tool::Rectangle:
        painter.drawRect(QRect(stroke_.start, stroke_.last).normalized());
        break;
    case Tool::Ellipse:
        painter.drawEllipse(QRect(stroke_.start, stroke_.last).normalized());
        break;
    case Tool::Text:
        if (!pendingText_.isEmpty()) {
            painter.drawPath(textPathFor(stroke_.start));
        }
        break;
    default:
        break;
    }
    painter.restore();
}

void PaintModule::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    QImage composed = flattenedImage();
    painter.drawImage(0, 0, composed);
    if (stroke_.active &&
        (currentTool_ == Tool::Line || currentTool_ == Tool::Rectangle ||
         currentTool_ == Tool::Ellipse || currentTool_ == Tool::Text)) {
        drawPreview(painter);
    } else if (stroke_.active && !stroke_.preview.isNull()) {
        // Live freehand preview: the in-progress raster is drawn over composition.
        painter.setOpacity(static_cast<qreal>(layers_[static_cast<std::size_t>(activeLayer_)].opacity) /
                           100.0);
        painter.drawImage(0, 0, stroke_.preview);
    }
}

void PaintModule::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || layers_.empty()) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint point = event->pos();
    stroke_.start = point;
    stroke_.last = point;
    stroke_.before = layers_[static_cast<std::size_t>(activeLayer_)].image.copy();
    stroke_.active = true;
    stroke_.preview = QImage();

    if (currentTool_ == Tool::Fill) {
        floodFill(point);
        finishStroke();
        return;
    }
    if (currentTool_ == Tool::Text) {
        bool accepted = false;
        const QString text = QInputDialog::getText(this, tr("Add text"), tr("Text:"),
                                                   QLineEdit::Normal, {}, &accepted);
        if (!accepted || text.isEmpty()) {
            stroke_.active = false;
            return;
        }
        pendingText_ = text;
        return;
    }

    paintAt(point, point);
    update();
}

void PaintModule::mouseMoveEvent(QMouseEvent* event)
{
    if (!stroke_.active || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    stroke_.last = event->pos();
    if (currentTool_ == Tool::Line || currentTool_ == Tool::Rectangle ||
        currentTool_ == Tool::Ellipse) {
        update();
        return;
    }
    paintAt(stroke_.last, event->lastPos());
    update();
}

void PaintModule::mouseReleaseEvent(QMouseEvent*)
{
    if (!stroke_.active) {
        return;
    }
    if (currentTool_ == Tool::Text && !pendingText_.isEmpty()) {
        applyTextToLayer();
    }
    pendingText_.clear();
    finishStroke();
}

void PaintModule::finishStroke()
{
    if (!stroke_.active) {
        return;
    }
    stroke_.active = false;
    stroke_.preview = QImage();
    pushStrokeCommand(stroke_.before);
    update();
}

void PaintModule::undo()
{
    if (undoStack_.canUndo()) {
        undoStack_.undo();
    }
}

void PaintModule::pushStrokeCommand(const QImage& before)
{
    auto* layer = mutableLayer();
    if (layer == nullptr) {
        return;
    }
    undoStack_.push(new CanvasCommand(this, activeLayer_, before, layer->image.copy(),
                                      tr("%1 stroke").arg(toolLabel())));
}

void PaintModule::redo()
{
    if (undoStack_.canRedo()) {
        undoStack_.redo();
    }
}

void PaintModule::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        stroke_.active = false;
        pendingText_.clear();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool PaintModule::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
            key->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

}  // namespace material_everything::paint
