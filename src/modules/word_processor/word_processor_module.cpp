#include "word_processor_module.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace me {
namespace word_processor {

WordProcessorModule::WordProcessorModule(QWidget *parent) : QWidget(parent) { buildUi(); }

void WordProcessorModule::buildUi() {
    auto *root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);
    root_layout->addWidget(buildRibbon());
    root_layout->addWidget(buildEditorArea(), 1);
    root_layout->addWidget(buildStatusBar());
}

QWidget *WordProcessorModule::buildRibbon() {
    auto *ribbon_scroll = new QScrollArea;
    ribbon_scroll->setWidgetResizable(true);
    ribbon_scroll->setFixedHeight(96);
    ribbon_scroll->setFrameShape(QFrame::NoFrame);

    auto *ribbon_content = new QWidget;
    auto *ribbon_layout = new QHBoxLayout(ribbon_content);
    ribbon_layout->setContentsMargins(12, 8, 12, 8);
    ribbon_layout->setSpacing(16);

    // File group.
    auto *file_group = new QWidget;
    auto *file_layout = new QVBoxLayout(file_group);
    file_layout->setContentsMargins(0, 0, 0, 0);
    file_layout->setSpacing(4);
    auto *file_label = new QLabel(QStringLiteral("File"));
    file_label->setStyleSheet(
        "font-size: 11px; color: palette(mid); letter-spacing: 1px; text-transform: uppercase;");
    auto *file_row = new QHBoxLayout;
    file_row->setSpacing(4);
    auto add_button = [this, &file_row](const QString &text, const char *slot) {
        QPushButton *button = new QPushButton(text);
        button->setToolTip(text);
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, SIGNAL(clicked()), this, slot);
        file_row->addWidget(button);
        return button;
    };
    add_button(QStringLiteral("New"), SLOT(newDocument()));
    add_button(QStringLiteral("Open"), SLOT(openDocument()));
    add_button(QStringLiteral("Save"), SLOT(saveDocument()));
    add_button(QStringLiteral("Save As"), SLOT(saveDocumentAs()));
    add_button(QStringLiteral("Export"), SLOT(exportDocument()));
    add_button(QStringLiteral("Print"), SLOT(printDocument()));
    file_layout->addWidget(file_row.data());  // placeholder; corrected below
    (void)file_row;

    return nullptr;  // replaced below
}
