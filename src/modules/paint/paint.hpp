#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QUndoStack>
#include <QWidget>

#include <array>
#include <optional>
#include <vector>

namespace material_everything::paint {

enum class Tool {
    Brush,
    Pencil,
    Eraser,
    Fill,
    Line,
    Rectangle,
    Ellipse,
    Text,
};

struct Layer {
    QString name;
    QImage image;
    bool visible = true;
    int opacity = 100;
};

class PaintModule : public QWidget {
    Q_OBJECT

public:
    explicit PaintModule(QWidget* parent = nullptr);

    static QString toolLabel(Tool tool);

    bool newCanvas(int width, int height);
    bool resizeCanvas(int width, int height, bool scaleContents = false);
    std::optional<QImage> exportImage(const QString& path) const;

    void setTool(Tool tool);
    void setColor(const QColor& color);
    void setBrushSize(int size);
    void setLayerOpacity(int layerIndex, int percent);
    void setLayerVisible(int layerIndex, bool visible);
    void addLayer();
    void removeLayer(int layerIndex);
    void selectLayer(int layerIndex);

    Tool tool() const { return currentTool_; }
    QColor color() const { return currentColor_; }
    int brushSize() const { return brushSize_; }
    int activeLayerIndex() const { return activeLayer_; }
    const std::vector<Layer>& layers() const { return layers_; }
    QUndoStack* undoStack() const { return &undoStack_; }
    Layer* layerAt(int index);
    QImage flattenedImage() const;

public slots:
    void undo();
    void redo();

signals:
    void canvasChanged();
    void layersChanged();
    void statusMessage(const QString& message, bool isError);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool event(QEvent*) override;

private:
    struct StrokeState {
        QImage before;
        QImage preview;
        QPoint start;
        QPoint last;
        bool active = false;
    };

    Layer* mutableLayer();
    std::vector<Layer> takeLayersSnapshot();
    void installLayers(std::vector<Layer> next, int selected);
    void restoreLayers(const std::vector<Layer>& previous, int selected);
    void insertLayer(int index, const Layer& layer);
    void eraseLayerOnly(int index, int minimumCount);
    QPainterPath textPathFor(const QPoint& position) const;
    void paintAt(const QPoint& current, const QPoint& previous);
    void floodFill(const QPoint& origin);
    void applyTextToLayer();
    void drawPreview(QPainter& painter) const;
    void finishStroke();
    void pushStrokeCommand(const QImage& before);
    void renderLayers(QImage& destination) const;

    std::vector<Layer> layers_;
    QUndoStack undoStack_;
    Tool currentTool_ = Tool::Brush;
    QColor currentColor_{Qt::black};
    int brushSize_ = 8;
    int activeLayer_ = 0;
    StrokeState stroke_;
    QString pendingText_;
    static constexpr int kMinimumCanvasSize = 1;
    static constexpr int kMaximumBrushSize = 256;
};

}  // namespace material_everything::paint
