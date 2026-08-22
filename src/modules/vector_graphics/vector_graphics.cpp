#include "vector_graphics.hpp"

#include <QApplication>
#include <QBrush>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>

namespace material_everything::vector_graphics {

namespace {

constexpr int kHandleSize = 8;

QPainterPath polygonPath(const QPointF& center, qreal radius, int sides) {
    QPainterPath path;
    if (sides < 3) return path;
    const qreal start = -M_PI / 2.0;
    path.moveTo(center.x() + radius * std::cos(start), center.y() + radius * std::sin(start));
    for (int i = 1; i < sides; ++i) {
        const qreal angle = start + 2.0 * M_PI * i / sides;
        path.lineTo(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }
    path.closeSubpath();
    return path;
}

QPen makePen(const QColor& color, qreal width) {
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

}  // namespace

VectorCanvas::VectorCanvas(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(QRectF(0, 0, 960, 640), this);
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setBackgroundBrush(QColor(0xF6, 0xFB, 0xFC));
    setStyleSheet(
        "QGraphicsView { background-color:#F6FBFC; border:1px solid #C4C7C5;"
        " border-radius:12px; }"
        "QGraphicsView:focus { border:2px solid #1A73E8; }");
}

void VectorCanvas::setTool(Tool tool) {
    tool_ = tool;
    clearNodeHandles();
    if (tool == Tool::Select) {
        setDragMode(QGraphicsView::RubberBandDrag);
    } else {
        setDragMode(QGraphicsView::NoDrag);
    }
    emit toolChanged(tool);
}

void VectorCanvas::setStroke(const QColor& color) { stroke_ = color; }
void VectorCanvas::setFill(const QColor& color) { fill_ = color; }

void VectorCanvas::setStrokeWidth(qreal width) { strokeWidth_ = qMax(0.5, width); }

void VectorCanvas::setPolygonSides(int sides) { polygonSides_ = qBound(3, sides, 64); }

void VectorCanvas::mousePressEvent(QMouseEvent* event) {
    const QPointF scenePoint = mapToScene(event->pos());
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    switch (tool_) {
        case Tool::Pen:
            beginPenSegment(scenePoint);
            return;
        case Tool::Rectangle:
        case Tool::Ellipse:
        case Tool::Polygon:
            pressPoint_ = scenePoint;
            return;
        case Tool::NodeEdit: {
            QGraphicsItem* item = itemAtPoint(scenePoint);
            if (item != activeNodeItem_) {
                activeNodeItem_ = item;
                updateNodeHandles();
            }
            return;
        }
        case Tool::Select:
            break;
    }
    QGraphicsView::mousePressEvent(event);
}

void VectorCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF scenePoint = mapToScene(event->pos());
    if (tool_ == Tool::Pen && penActive_) {
        appendPenSegment(scenePoint);
        return;
    }
    if (tool_ == Tool::Rectangle || tool_ == Tool::Ellipse || tool_ == Tool::Polygon) {
        if (event->buttons() & Qt::LeftButton) {
            const QRectF rect = QRectF(pressPoint_, scenePoint).normalized();
            delete draftItem_;
            draftItem_ = nullptr;
            QPainterPath path;
            switch (tool_) {
                case Tool::Rectangle:
                    path.addRoundedRect(rect, 8, 8);
                    break;
                case Tool::Ellipse:
                    path.addEllipse(rect);
                    break;
                case Tool::Polygon:
                    path = polygonPath(rect.center(), qMin(rect.width(), rect.height()) / 2.0, polygonSides_);
                    break;
                default:
                    break;
            }
            draftItem_ = scene_->addPath(path, makePen(stroke_, strokeWidth_), fill_);
            draftItem_->setOpacity(0.75);
        }
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void VectorCanvas::mouseReleaseEvent(QMouseEvent* event) {
    const QPointF scenePoint = mapToScene(event->pos());
    if (tool_ == Tool::Pen && penActive_) {
        appendPenSegment(scenePoint);
        penActive_ = false;
        finishShape(penPath_);
        penPath_ = QPainterPath();
        return;
    }
    if ((tool_ == Tool::Rectangle || tool_ == Tool::Ellipse || tool_ == Tool::Polygon) && draftItem_) {
        draftItem_->setOpacity(1.0);
        draftItem_ = nullptr;
        emit layersChanged();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

QGraphicsItem* VectorCanvas::itemAtPoint(const QPointF& scenePoint) const {
    const QList<QGraphicsItem*> items = scene_->items(scenePoint, Qt::IntersectsItemShape,
                                                      Qt::DescendingOrder);
    for (QGraphicsItem* item : items) {
        if (!nodeHandles_.empty() &&
            std::find(nodeHandles_.begin(), nodeHandles_.end(), item) != nodeHandles_.end()) {
            continue;
        }
        return item;
    }
    return nullptr;
}

void VectorCanvas::finishShape(const QPainterPath& path) {
    if (path.isEmpty()) return;
    scene_->addPath(path, makePen(stroke_, strokeWidth_), fill_);
    emit layersChanged();
}

void VectorCanvas::beginPenSegment(const QPointF& point) {
    penPath_ = QPainterPath(point);
    penActive_ = true;
    appendPenSegment(point);
}

void VectorCanvas::appendPenSegment(const QPointF& point) {
    if (!penActive_ || penPath_.isEmpty()) return;
    penPath_.lineTo(point);
    delete draftItem_;
    draftItem_ = scene_->addPath(penPath_, makePen(stroke_, strokeWidth_), Qt::NoBrush);
}

void VectorCanvas::clearNodeHandles() {
    for (QGraphicsRectItem* handle : nodeHandles_) {
        scene_->removeItem(handle);
        delete handle;
    }
    nodeHandles_.clear();
}

void VectorCanvas::updateNodeHandles() {
    clearNodeHandles();
    if (!activeNodeItem_) return;
    const QPainterPathStroker stroker;
    QPainterPath shape = activeNodeItem_->shape();
    const QRectF bounds = activeNodeItem_->boundingRect();
    const int divisions = 8;
    for (int i = 0; i < divisions; ++i) {
        const qreal t = static_cast<qreal>(i) / divisions;
        const QPointF point(bounds.left() + bounds.width() * t, bounds.top());
        auto* handle = scene_->addRect(QRectF(point - QPointF(kHandleSize / 2.0, kHandleSize / 2.0),
                                              QSizeF(kHandleSize, kHandleSize)),
                                       QPen(QColor(0x1A, 0x73, 0xE8), 1.5),
                                       QBrush(Qt::white));
        handle->setFlag(QGraphicsItem::ItemIsSelectable, true);
        nodeHandles_.push_back(handle);
    }
    Q_UNUSED(stroker);
}

void VectorCanvas::alignSelection(AlignMode mode) {
    const QList<QGraphicsItem*> selection = scene_->selectedItems();
    if (selection.isEmpty()) return;
    QList<QGraphicsItem*> mutableSelection = selection;

    QRectF unionRect = selection.first()->sceneBoundingRect();
    for (QGraphicsItem* item : selection) unionRect = unionRect.united(item->sceneBoundingRect());

    if (mode == AlignMode::DistributeH || mode == AlignMode::DistributeV) {
        if (selection.size() < 3) return;
        std::sort(mutableSelection.begin(), mutableSelection.end(), [mode](QGraphicsItem* a, QGraphicsItem* b) {
            if (mode == AlignMode::DistributeH) {
                return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x();
            }
            return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y();
        });
        const QGraphicsItem* first = mutableSelection.first();
        const QGraphicsItem* last = mutableSelection.last();
        const qreal firstPos = (mode == AlignMode::DistributeH) ? first->sceneBoundingRect().center().x()
                                                                : first->sceneBoundingRect().center().y();
        const qreal lastPos = (mode == AlignMode::DistributeH) ? last->sceneBoundingRect().center().x()
                                                               : last->sceneBoundingRect().center().y();
        const int count = static_cast<int>(selection.size());
        for (int i = 1; i < count - 1; ++i) {
            QGraphicsItem* item = mutableSelection[i];
            const qreal target = firstPos + (lastPos - firstPos) * i / (count - 1);
            if (mode == AlignMode::DistributeH) {
                item->moveBy(target - item->sceneBoundingRect().center().x(), 0);
            } else {
                item->moveBy(0, target - item->sceneBoundingRect().center().y());
            }
        }
        return;
    }

    for (QGraphicsItem* item : selection) {
        const QRectF bounds = item->sceneBoundingRect();
        switch (mode) {
            case AlignMode::Left:
                item->moveBy(unionRect.left() - bounds.left(), 0);
                break;
            case AlignMode::HCenter:
                item->moveBy(unionRect.center().x() - bounds.center().x(), 0);
                break;
            case AlignMode::Right:
                item->moveBy(unionRect.right() - bounds.right(), 0);
                break;
            case AlignMode::Top:
                item->moveBy(0, unionRect.top() - bounds.top());
                break;
            case AlignMode::VCenter:
                item->moveBy(0, unionRect.center().y() - bounds.center().y());
                break;
            case AlignMode::Bottom:
                item->moveBy(0, unionRect.bottom() - bounds.bottom());
                break;
            case AlignMode::DistributeH:
            case AlignMode::DistributeV:
                break;
        }
    }
}

bool VectorCanvas::exportSvg(const QString& path) {
    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(scene_->sceneRect().size().toSize());
    generator.setViewBox(scene_->sceneRect());
    generator.setTitle(QStringLiteral("Material Everything vector export"));
    generator.setDescription(QStringLiteral("Exported from Material Everything vector canvas"));

    QPainter painter;
    if (!painter.begin(&generator)) return false;
    scene_->render(&painter, QRectF(), scene_->sceneRect());
    painter.end();
    return true;
}

bool VectorCanvas::exportPng(const QString& path, const QSize& size) {
    const QRectF sourceRect = scene_->sceneRect();
    QSize target = size;
    if (target.isEmpty()) target = sourceRect.size().toSize();
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene_->render(&painter, QRectF(image.rect()), sourceRect);
    painter.end();
    return image.save(path, "PNG");
}

bool VectorCanvas::importSvg(const QString& path) {
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return false;

    const QRectF viewBox = renderer.viewBoxF();
    const QRectF targetRect = scene_->sceneRect();
    QImage image(targetRect.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(image.rect()));
    painter.end();

    QGraphicsPixmapItem* pixmapItem =
        scene_->addPixmap(QPixmap::fromImage(image));
    pixmapItem->setPos(0, 0);
    pixmapItem->setData(0, QStringLiteral("svg-import"));
    emit layersChanged();
    return true;
}

int VectorCanvas::layerCount() const {
    return static_cast<int>(scene_->items().size());
}

LayerInfo VectorCanvas::layerInfo(int index) const {
    const QList<QGraphicsItem*> items = scene_->items();
    LayerInfo info;
    if (index < 0 || index >= items.size()) return info;
    QGraphicsItem* item = items[index];
    info.name = QStringLiteral("Shape %1").arg(index + 1);
    info.visible = item->isVisible();
    info.locked = !(item->flags() & QGraphicsItem::ItemIsSelectable);
    info.opacity = static_cast<int>(item->opacity() * 100);
    return info;
}

void VectorCanvas::addLayer(const QString& name) {
    Q_UNUSED(name);
    emit layersChanged();
}

void VectorCanvas::removeLayer(int index) {
    const QList<QGraphicsItem*> items = scene_->items();
    if (index < 0 || index >= items.size()) return;
    scene_->removeItem(items[index]);
    delete items[index];
    emit layersChanged();
}

void VectorCanvas::selectLayer(int index) {
    const QList<QGraphicsItem*> items = scene_->items();
    if (index < 0 || index >= items.size()) return;
    scene_->clearSelection();
    items[index]->setSelected(true);
    activeNodeItem_ = items[index];
    updateNodeHandles();
    emit selectionChanged();
}

void VectorCanvas::setLayerVisible(int index, bool visible) {
    const QList<QGraphicsItem*> items = scene_->items();
    if (index < 0 || index >= items.size()) return;
    items[index]->setVisible(visible);
    emit layersChanged();
}

void VectorCanvas::setLayerLocked(int index, bool locked) {
    const QList<QGraphicsItem*> items = scene_->items();
    if (index < 0 || index >= items.size()) return;
    QGraphicsItem* item = items[index];
    item->setFlag(QGraphicsItem::ItemIsSelectable, !locked);
    item->setFlag(QGraphicsItem::ItemIsMovable, !locked);
    emit layersChanged();
}

void VectorCanvas::setLayerOpacity(int index, int percent) {
    const QList<QGraphicsItem*> items = scene_->items();
    if (index < 0 || index >= items.size()) return;
    items[index]->setOpacity(qBound(0, percent, 100) / 100.0);
    emit layersChanged();
}

int VectorCanvas::activeLayer() const {
    const QList<QGraphicsItem*> selection = scene_->selectedItems();
    if (selection.isEmpty()) return -1;
    return scene_->items().indexOf(selection.first());
}

VectorGraphicsModule::VectorGraphicsModule(QWidget* parent) : QWidget(parent) {
    buildUi();
}

void VectorGraphicsModule::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    layout->addWidget(buildToolbar());

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    canvas_ = new VectorCanvas(this);
    splitter->addWidget(canvas_);

    auto* sidePanel = new QWidget(this);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(new QLabel(QStringLiteral("Layers"), sidePanel));
    layerList_ = new QListWidget(sidePanel);
    sideLayout->addWidget(layerList_, 2);
    sideLayout->addWidget(buildLayersPanel());

    sideLayout->addWidget(new QLabel(QStringLiteral("Shapes"), sidePanel));
    shapeList_ = new QListWidget(sidePanel);
    sideLayout->addWidget(shapeList_, 1);
    splitter->addWidget(sidePanel);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(canvas_, &VectorCanvas::layersChanged, this, &VectorGraphicsModule::refreshLayerList);
    refreshLayerList();
}

QWidget* VectorGraphicsModule::buildToolbar() {
    auto* bar = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    toolBox_ = new QComboBox(bar);
    toolBox_->addItem(QStringLiteral("Select"), static_cast<int>(Tool::Select));
    toolBox_->addItem(QStringLiteral("Pen"), static_cast<int>(Tool::Pen));
    toolBox_->addItem(QStringLiteral("Rectangle"), static_cast<int>(Tool::Rectangle));
    toolBox_->addItem(QStringLiteral("Ellipse"), static_cast<int>(Tool::Ellipse));
    toolBox_->addItem(QStringLiteral("Polygon"), static_cast<int>(Tool::Polygon));
    toolBox_->addItem(QStringLiteral("Node edit"), static_cast<int>(Tool::NodeEdit));
    layout->addWidget(toolBox_);

    layout->addWidget(new QLabel(QStringLiteral("Stroke width"), bar));
    strokeWidth_ = new QSpinBox(bar);
    strokeWidth_->setRange(1, 64);
    strokeWidth_->setValue(2);
    layout->addWidget(strokeWidth_);

    layout->addWidget(new QLabel(QStringLiteral("Polygon sides"), bar));
    polygonSides_ = new QSpinBox(bar);
    polygonSides_->setRange(3, 64);
    polygonSides_->setValue(6);
    layout->addWidget(polygonSides_);

    auto* strokeButton = new QPushButton(QStringLiteral("Stroke…"), bar);
    auto* fillButton = new QPushButton(QStringLiteral("Fill…"), bar);
    layout->addWidget(strokeButton);
    layout->addWidget(fillButton);

    auto* importButton = new QPushButton(QStringLiteral("Import SVG…"), bar);
    auto* exportSvgButton = new QPushButton(QStringLiteral("Export SVG…"), bar);
    auto* exportPngButton = new QPushButton(QStringLiteral("Export PNG…"), bar);
    layout->addWidget(importButton);
    layout->addWidget(exportSvgButton);
    layout->addWidget(exportPngButton);

    layout->addStretch(1);

    connect(toolBox_, &QComboBox::activated, this, [this](int index) {
        canvas_->setTool(static_cast<Tool>(toolBox_->itemData(index).toInt()));
    });
    connect(strokeWidth_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        canvas_->setStrokeWidth(value);
    });
    connect(polygonSides_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        canvas_->setPolygonSides(value);
    });
    connect(strokeButton, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(canvas_->property("stroke").value<QColor>(), this,
                                                    QStringLiteral("Stroke colour"));
        if (color.isValid()) {
            canvas_->setStroke(color);
            canvas_->setProperty("stroke", color);
        }
    });
    connect(fillButton, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(canvas_->property("fill").value<QColor>(), this,
                                                    QStringLiteral("Fill colour"));
        if (color.isValid()) {
            canvas_->setFill(color);
            canvas_->setProperty("fill", color);
        }
    });
    connect(importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import SVG"), QString(),
                                                          QStringLiteral("SVG files (*.svg)"));
        if (!path.isEmpty()) importSvg(path);
    });
    connect(exportSvgButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export SVG"), QString(),
                                                          QStringLiteral("SVG files (*.svg)"));
        if (!path.isEmpty()) exportSvg(path);
    });
    connect(exportPngButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export PNG"), QString(),
                                                          QStringLiteral("PNG images (*.png)"));
        if (!path.isEmpty()) exportPng(path);
    });

    return bar;
}

QWidget* VectorGraphicsModule::buildLayersPanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 4, 0, 4);

    auto* addLayer = new QPushButton(QStringLiteral("Add"), panel);
    auto* removeLayer = new QPushButton(QStringLiteral("Remove"), panel);
    auto* alignLeft = new QPushButton(QStringLiteral("Align left"), panel);
    auto* alignHCenter = new QPushButton(QStringLiteral("Center"), panel);
    auto* distributeH = new QPushButton(QStringLiteral("Distribute"), panel);
    layout->addWidget(addLayer);
    layout->addWidget(removeLayer);
    layout->addWidget(alignLeft);
    layout->addWidget(alignHCenter);
    layout->addWidget(distributeH);
    layout->addStretch(1);

    connect(addLayer, &QPushButton::clicked, this, [this] { canvas_->addLayer(QStringLiteral("Layer")); });
    connect(removeLayer, &QPushButton::clicked, this,
            [this] { canvas_->removeLayer(layerList_->currentRow()); });
    connect(alignLeft, &QPushButton::clicked, this, [this] { canvas_->alignSelection(AlignMode::Left); });
    connect(alignHCenter, &QPushButton::clicked, this,
            [this] { canvas_->alignSelection(AlignMode::HCenter); });
    connect(distributeH, &QPushButton::clicked, this,
            [this] { canvas_->alignSelection(AlignMode::DistributeH); });

    return panel;
}

void VectorGraphicsModule::refreshLayerList() {
    if (!layerList_) return;
    const int previousRow = layerList_->currentRow();
    layerList_->clear();
    for (int i = 0; i < canvas_->layerCount(); ++i) {
        const LayerInfo info = canvas_->layerInfo(i);
        layerList_->addItem(info.name);
    }
    if (previousRow >= 0 && previousRow < layerList_->count()) layerList_->setCurrentRow(previousRow);
}

bool VectorGraphicsModule::importSvg(const QString& path) { return canvas_->importSvg(path); }
bool VectorGraphicsModule::exportSvg(const QString& path) { return canvas_->exportSvg(path); }
bool VectorGraphicsModule::exportPng(const QString& path) { return canvas_->exportPng(path); }

}  // namespace material_everything::vector_graphics
