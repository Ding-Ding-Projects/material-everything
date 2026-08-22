#pragma once

#include "paint.hpp"

#include <QDialog>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QToolButton;

namespace material_everything::paint {

class InfiniteColorPicker final : public QDialog {
    Q_OBJECT

public:
    explicit InfiniteColorPicker(QWidget* parent = nullptr);
    QColor selectedColor() const;

signals:
    void colorChosen(const QColor& color);

private:
    void updateFromHex();
    void updateFromSliders();
    void emitColor();
    class RainbowWidget* rainbow_ = nullptr;
    QSlider* hue_ = nullptr;
    QSlider* saturation_ = nullptr;
    QSlider* value_ = nullptr;
    QSlider* alpha_ = nullptr;
    QLineEdit* hex_ = nullptr;
};

class PaintWindow final : public QWidget {
    Q_OBJECT

public:
    explicit PaintWindow(QWidget* parent = nullptr);

private slots:
    void chooseTool(int index);
    void openColorPicker();
    void refreshLayers();
    void exportImage();
    void resizeCanvasRequested();

private:
    QWidget* buildToolbar();
    QWidget* buildLayerPanel();
    void syncToolSelection();
    void showStatus(const QString& message, bool isError);

    PaintModule* canvas_ = nullptr;
    QListWidget* layerList_ = nullptr;
    QSlider* opacity_ = nullptr;
    QSpinBox* brushSize_ = nullptr;
    QToolButton* colorButton_ = nullptr;
    QColor currentColor_{Qt::black};
    std::array<QToolButton*, 8> toolButtons_{};
};

}  // namespace material_everything::paint
