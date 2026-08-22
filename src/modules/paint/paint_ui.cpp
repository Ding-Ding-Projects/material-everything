#include "paint_ui.hpp"

#include <QColor>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace material_everything::paint {
namespace {

constexpr int kM3Radius = 20;

QString toolName(int index)
{
    const auto value = static_cast<Tool>(index);
    return PaintModule::toolLabel(value);
}

QToolButton* makeToolButton(Tool tool)
{
    auto* button = new QToolButton;
    button->setText(PaintModule::toolLabel(tool));
    button->setCheckable(true);
    button->setToolTip(PaintModule::toolLabel(tool));
    button->setProperty("md3Role", QStringLiteral("tonal"));
    return button;
}

}  // namespace

class RainbowWidget final : public QWidget {
public:
    using QWidget::QWidget;

    void setHue(int hue)
    {
        hue_ = std::clamp(hue, 0, 359);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int x = 0; x < width(); ++x) {
            QColor color;
            color.setHslF(static_cast<qreal>(x) / width(), 1.0, 0.5);
            painter.setPen(color);
            painter.drawLine(x, 0, x, height());
        }
    }

private:
    int hue_ = 0;
};

InfiniteColorPicker::InfiniteColorPicker(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Infinite colour picker"));
    setModal(false);

    rainbow_ = new RainbowWidget(this);
    rainbow_->setFixedHeight(36);
    rainbow_->setMinimumWidth(280);

    auto makeSlider = [this](int max) {
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, max);
        slider->setValue(max == 359 ? 0 : 255);
        slider->setMinimumHeight(44);
        return slider;
    };
    hue_ = makeSlider(359);
    saturation_ = makeSlider(255);
    value_ = makeSlider(255);
    alpha_ = makeSlider(255);
    hex_ = new QLineEdit(QStringLiteral("#000000"), this);
    hex_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6,8}$")), this));

    auto* form = new QFormLayout;
    form->addRow(tr("Rainbow"), rainbow_);
    form->addRow(tr("Hue"), hue_);
    form->addRow(tr("Saturation"), saturation_);
    form->addRow(tr("Brightness"), value_);
    form->addRow(tr("Alpha"), alpha_);
    form->addRow(tr("HEX / RGBA"), hex_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->addLayout(form);

    connect(hue_, &QSlider::valueChanged, this, &InfiniteColorPicker::updateFromSliders);
    connect(saturation_, &QSlider::valueChanged, this, &InfiniteColorPicker::updateFromSliders);
    connect(value_, &QSlider::valueChanged, this, &InfiniteColorPicker::updateFromSliders);
    connect(alpha_, &QSlider::valueChanged, this, &InfiniteColorPicker::updateFromSliders);
    connect(hex_, &QLineEdit::textEdited, this, &InfiniteColorPicker::updateFromHex);

    setStyleSheet(QStringLiteral(
        "QDialog { background:%1; border-radius:%2px; }"
        "QLineEdit { border-radius:12px; padding:10px; }")
        .arg(palette().color(QPalette::Base).name())
        .arg(kM3Radius));
}

QColor InfiniteColorPicker::selectedColor() const
{
    QColor result;
    result.setHsv(hue_->value(), saturation_->value(), value_->value(), alpha_->value());
    return result;
}

void InfiniteColorPicker::updateFromSliders()
{
    rainbow_->setHue(hue_->value());
    const QColor next = selectedColor();
    hex_->setText(next.name(QColor::HexArgb).toUpper());
    emitColor();
}

void InfiniteColorPicker::updateFromHex()
{
    const QColor parsed(hex_->text());
    if (!parsed.isValid()) {
        return;
    }
    hue_->blockSignals(true);
    saturation_->blockSignals(true);
    value_->blockSignals(true);
    alpha_->blockSignals(true);
    hue_->setValue(parsed.hue());
    saturation_->setValue(parsed.saturation());
    value_->setValue(parsed.value());
    alpha_->setValue(parsed.alpha());
    hue_->blockSignals(false);
    saturation_->blockSignals(false);
    value_->blockSignals(false);
    alpha_->blockSignals(false);
    rainbow_->setHue(parsed.hue());
    emitColor();
}

void InfiniteColorPicker::emitColor()
{
    emit colorChosen(selectedColor());
}

PaintWindow::PaintWindow(QWidget* parent) : QWidget(parent)
{
    canvas_ = new PaintModule(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(canvas_);
    scroll->setWidgetResizable(false);
    scroll->setBackgroundRole(QPalette::Dark);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildToolbar());
    root->addWidget(scroll, 1);
    root->addWidget(buildLayerPanel());

    connect(canvas_, &PaintModule::statusMessage, this,
            [this](const QString& message, bool isError) { showStatus(message, isError); });
    connect(canvas_->undoStack(), &QUndoStack::canUndoChanged, this,
            [this](bool canUndo) { showStatus(canUndo ? tr("Undo available") : tr("Nothing to undo"),
                                                false); });

    setStyleSheet(QStringLiteral("QWidget#paintRoot { background:%1; }").arg(
        palette().color(QPalette::Base).name()));
    setObjectName(QStringLiteral("paintRoot"));
    setWindowTitle(tr("Paint"));
}

QWidget* PaintWindow::buildToolbar()
{
    auto* toolbar = new QWidget(this);
    toolbar->setFixedWidth(96);
    toolbar->setObjectName(QStringLiteral("paintToolbar"));
    auto* layout = new QVBoxLayout(toolbar);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(8);

    constexpr std::array<Tool, 8> tools{Tool::Brush, Tool::Pencil, Tool::Eraser, Tool::Fill,
                                        Tool::Line, Tool::Rectangle, Tool::Ellipse, Tool::Text};
    for (std::size_t i = 0; i < tools.size(); ++i) {
        auto* button = makeToolButton(tools[i]);
        toolButtons_[i] = button;
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, index = static_cast<int>(i)] { chooseTool(index); });
    }
    layout->addSpacing(12);

    colorButton_ = new QToolButton(toolbar);
    colorButton_->setText(tr("Colour"));
    colorButton_->setToolTip(tr("Open the infinite colour picker"));
    colorButton_->setMinimumSize(72, 48);
    connect(colorButton_, &QToolButton::clicked, this, &PaintWindow::openColorPicker);
    layout->addWidget(colorButton_);

    auto* sizeLabel = new QLabel(tr("Brush size"), toolbar);
    brushSize_ = new QSpinBox(toolbar);
    brushSize_->setRange(1, PaintModule().brushSizeLimitHintForUi());
    brushSize_->setValue(8);
    connect(brushSize_, qOverload<int>(&QSpinBox::valueChanged), canvas_,
            &PaintModule::setBrushSize);
    layout->addWidget(sizeLabel);
    layout->addWidget(brushSize_);
    layout->addStretch(1);

    toolbar->setStyleSheet(QStringLiteral(
        "#paintToolbar { background:%1; border-radius:0 %2px %2px 0; }"
        "QToolButton { min-height:40px; border-radius:18px; padding:6px; }"
        "QToolButton:checked { background:rgba(103,80,164,.22); font-weight:600; }")
        .arg(palette().color(QPalette::SurfaceContainerHighest).name())
        .arg(kM3Radius / 2));
    syncToolSelection();
    return toolbar;
}

QWidget* PaintWindow::buildLayerPanel()
{
    auto* panel = new QWidget(this);
    panel->setFixedWidth(240);
    panel->setObjectName(QStringLiteral("paintLayers"));
    auto* root = new QVBoxLayout(panel);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto* title = new QLabel(tr("Layers"), panel);
    title->setStyleSheet(QStringLiteral("font-weight:600"));
    root->addWidget(title);

    layerList_ = new QListWidget(panel);
    layerList_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(layerList_, &QListWidget::currentRowChanged, canvas_, &PaintModule::selectLayer);
    root->addWidget(layerList_, 1);

    auto* opacityLabel = new QLabel(tr("Layer opacity"), panel);
    opacity_ = new QSlider(Qt::Horizontal, panel);
    opacity_->setRange(0, 100);
    opacity_->setValue(100);
    connect(opacity_, &QSlider::valueChanged, canvas_,
            [this](int value) { canvas_->setLayerOpacity(canvas_->activeLayerIndex(), value); });

    auto* addButton = new QToolButton(panel);
    addButton->setText(tr("Add layer"));
    connect(addButton, &QToolButton::clicked, canvas_, &PaintModule::addLayer);

    auto* removeButton = new QToolButton(panel);
    removeButton->setText(tr("Remove active layer"));
    connect(removeButton, &QToolButton::clicked, canvas_,
            [this] { canvas_->removeLayer(canvas_->activeLayerIndex()); });

    auto* undoButton = new QToolButton(panel);
    undoButton->setText(tr("Undo (Ctrl+Z)"));
    connect(undoButton, &QToolButton::clicked, canvas_, &PaintModule::undo);

    auto* redoButton = new QToolButton(panel);
    redoButton->setText(tr("Redo (Ctrl+Shift+Z)"));
    connect(redoButton, &QToolButton::clicked, canvas_, &PaintModule::redo);

    auto* resizeButton = new QToolButton(panel);
    resizeButton->setText(tr("Resize canvas…"));
    connect(resizeButton, &QToolButton::clicked, this, &PaintWindow::resizeCanvasRequested);

    auto* exportButton = new QToolButton(panel);
    exportButton->setText(tr("Export PNG/JPG…"));
    connect(exportButton, &QToolButton::clicked, this, &PaintWindow::exportImage);

    root->addWidget(opacityLabel);
    root->addWidget(opacity_);
    for (auto* control : {addButton, removeButton, undoButton, redoButton, resizeButton,
                          exportButton}) {
        control->setMinimumHeight(40);
        root->addWidget(control);
    }

    connect(canvas_, &PaintModule::layersChanged, this, &PaintWindow::refreshLayers);
    refreshLayers();

    panel->setStyleSheet(QStringLiteral(
        "#paintLayers { background:%1; } QToolButton { border-radius:18px; padding:8px; }")
        .arg(palette().color(QPalette::SurfaceContainerLow).name()));
    return panel;
}

void PaintWindow::chooseTool(int index)
{
    if (index >= 0 && index < static_cast<int>(toolButtons_.size())) {
        canvas_->setTool(static_cast<Tool>(index));
        syncToolSelection();
    }
}

void PaintWindow::syncToolSelection()
{
    const int active = static_cast<int>(canvas_->tool());
    for (int i = 0; i < static_cast<int>(toolButtons_.size()); ++i) {
        if (toolButtons_[static_cast<std::size_t>(i)] != nullptr) {
            toolButtons_[static_cast<std::size_t>(i)]->setChecked(i == active);
            toolButtons_[static_cast<std::size_t>(i)]->setAccessibleName(
                tr("%1 tool%2").arg(toolName(i)).arg(i == active ? tr(", selected") : QString{}));
        }
    }
}

void PaintWindow::openColorPicker()
{
    auto picker = std::make_unique<InfiniteColorPicker>(this);
    connect(picker.get(), &InfiniteColorPicker::colorChosen, this,
            [this](const QColor& color) {
                currentColor_ = color;
                canvas_->setColor(color);
                colorButton_->setText(color.name(QColor::HexArgb).toUpper());
                QString swatchStyle =
                    QStringLiteral("background:%1;border-radius:14px;color:#fff;padding:8px;")
                        .arg(color.name());
                colorButton_->setStyleSheet(swatchStyle);
            });
    picker->show();
    // Keep ownership while modeless.
    picker.release()->setAttribute(Qt::WA_DeleteOnClose);
}

void PaintWindow::refreshLayers()
{
    if (layerList_ == nullptr) {
        return;
    }
    layerList_->blockSignals(true);
    layerList_->clear();
    for (const Layer& layer : canvas_->layers()) {
        auto* item = new QListWidgetItem(layer.visible ? layer.name : layer.name + tr(" (hidden)"),
                                         layerList_);
        item->setData(Qt::UserRole, layer.opacity);
        layerList_->addItem(item);
    }
    layerList_->setCurrentRow(std::clamp(canvas_->activeLayerIndex(), 0,
                                          layerList_->count() - 1));
    layerList_->blockSignals(false);
    opacity_->setValue(canvas_->layers()[static_cast<std::size_t>(
        std::clamp(canvas_->activeLayerIndex(), 0, static_cast<int>(canvas_->layers().size()) - 1))]
                          .opacity);
}

void PaintWindow::resizeCanvasRequested()
{
    const QSize current(canvas_->flattenedImage().size());
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Resize canvas"));
    dialog.setLabelText(tr("Enter width,height in pixels (current %1×%2):")
                            .arg(current.width()).arg(current.height()));
    dialog.setTextValue(QStringLiteral("%1,%2").arg(current.width()).arg(current.height()));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QStringList parts = dialog.textValue().split(QLatin1Char(','));
    if (parts.size() != 2) {
        showStatus(tr("Use width,height."), true);
        return;
    }
    canvas_->resizeCanvas(parts[0].toInt(), parts[1].toInt(), true);
}

void PaintWindow::exportImage()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export image"), {},
                                                      tr("PNG images (*.png);;JPEG images (*.jpg *.jpeg)"));
    if (!path.isEmpty()) {
        canvas_->exportImage(path);
    }
}

void PaintWindow::showStatus(const QString& message, bool isError)
{
    setAccessibleDescription(message);
    setToolTip(message);
    statusMessage_ = message;
    statusIsError_ = isError;
    Q_EMIT statusUpdated(message, isError);
}

}  // namespace material_everything::paint
