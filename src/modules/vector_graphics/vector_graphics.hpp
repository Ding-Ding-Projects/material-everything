#pragma once

#include <QColor>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QSpinBox>
#include <QSvgRenderer>
#include <QWidget>

#include <memory>
#include <vector>

class QSvgGenerator;

namespace material_everything::vector_graphics {

enum class Tool {
    Select,
    Pen,
    Rectangle,
    Ellipse,
    Polygon,
    NodeEdit,
};

enum class AlignMode {
    Left,
    HCenter,
    Right,
    Top,
    VCenter,
    Bottom,
    DistributeH,
    DistributeV,
};

struct LayerInfo {
    QString name;
    bool visible = true;
    bool locked = false;
    int opacity = 100;
};

class VectorCanvas final : public QGraphicsView {
    Q_OBJECT

public:
    explicit VectorCanvas(QWidget* parent = nullptr);

    void setTool(Tool tool);
    void setStroke(const QColor& color);
    void setFill(const QColor& color);
    void setStrokeWidth(qreal width);
    void setPolygonSides(int sides);

    bool exportSvg(const QString& path);
    bool exportPng(const QString& path, const QSize& size = QSize(0, 0));
    bool importSvg(const QString& path);

    void alignSelection(AlignMode mode);

    int layerCount() const;
    LayerInfo layerInfo(int index) const;
    void addLayer(const QString& name);
    void removeLayer(int index);
    void selectLayer(int index);
    void setLayerVisible(int index, bool visible);
    void setLayerLocked(int index, bool locked);
    void setLayerOpacity(int index, int percent);
    int activeLayer() const;

signals:
    void toolChanged(Tool tool);
    void selectionChanged();
    void layersChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QGraphicsItem* itemAtPoint(const QPointF& scenePoint) const;
    void finishShape(const QPainterPath& path);
    void applyLayerState();
    void updateNodeHandles();
    void clearNodeHandles();
    void beginPenSegment(const QPointF& point);
    void appendPenSegment(const QPointF& point);

    QGraphicsScene* scene_ = nullptr;
    Tool tool_ = Tool::Select;
    QColor stroke_ = QColor(0x1A, 0x73, 0xE8);
    QColor fill_ = QColor(0, 0, 0, 0);
    qreal strokeWidth_ = 2.0;
    int polygonSides_ = 6;

    QGraphicsItem* draftItem_ = nullptr;
    QPointF pressPoint_;
    bool penActive_ = false;
    QPainterPath penPath_;

    QGraphicsItem* activeNodeItem_ = nullptr;
    std::vector<QGraphicsRectItem*> nodeHandles_;
};

class VectorGraphicsModule final : public QWidget {
    Q_OBJECT

public:
    explicit VectorGraphicsModule(QWidget* parent = nullptr);

    VectorCanvas* canvas() const;
    bool importSvg(const QString& path);
    bool exportSvg(const QString& path);
    bool exportPng(const QString& path);

private:
    void buildUi();
    QWidget* buildToolbar();
    QWidget* buildLayersPanel();
    void refreshLayerList();

    VectorCanvas* canvas_;
    QListWidget* layerList_;
    QListWidget* shapeList_;
    QComboBox* toolBox_;
    QSpinBox* strokeWidth_;
    QSpinBox* polygonSides_;
};

}  // namespace material_everything::vector_graphics
