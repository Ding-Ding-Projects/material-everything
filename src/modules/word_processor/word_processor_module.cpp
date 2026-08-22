#include "word_processor_module.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace me {
namespace word_processor {

WordProcessorModule::WordProcessorModule(QWidget *parent) : QWidget(parent) { buildUi(); }

void WordProcessorModule::buildUi() {
    QVBoxLayout *root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);
    root_layout->addWidget(buildRibbon());
    root_layout->addWidget(buildEditorArea(), 1);
    root_layout->addWidget(buildStatusBar());
}

QWidget *WordProcessorModule::buildRibbon() {
    QScrollArea *ribbon_scroll = new QScrollArea;
    ribbon_scroll->setWidgetResizable(true);
    ribbon_scroll->setFixedHeight(96);
    ribbon_scroll->setFrameShape(QFrame::NoFrame);

    QWidget *ribbon_content = new QWidget;
    QHBoxLayout *ribbon_layout = new QHBoxLayout(ribbon_content);
    ribbon_layout->setContentsMargins(12, 8, 12, 8);
    ribbon_layout->setSpacing(16);

    auto make_group = [ribbon_layout](const QString &label) -> QWidget * {
        QWidget *group = new QWidget;
        QVBoxLayout *group_layout = new QVBoxLayout(group);
        group_layout->setContentsMargins(0, 0, 0, 0);
        group_layout->setSpacing(4);

        QLabel *label_widget = new QLabel(label);
        label_widget->setStyleSheet("font-size: 11px; color: palette(mid); letter-spacing: 1px;");

        QWidget *row = new QWidget;
        QHBoxLayout *row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);

        group_layout->addWidget(row);
        group_layout->addWidget(label_widget);
        ribbon_layout->addWidget(group);
        return row;
    };

    auto add_button = [](QWidget *group_row, const QString &text, const QObject *receiver,
                         const char *slot) {
        QPushButton *button = new QPushButton(text);
        button->setToolTip(text);
        button->setFocusPolicy(Qt::StrongFocus);
        QObject::connect(button, SIGNAL(clicked()), receiver, slot);
        group_row->layout()->addWidget(button);
        return button;
    };

    auto add_toggle = [](QWidget *group_row, const QString &text, const QObject *receiver,
                         const char *slot) {
        QPushButton *button = new QPushButton(text);
        button->setCheckable(true);
        button->setToolTip(text);
        QObject::connect(button, SIGNAL(toggled(bool)), receiver, slot);
        group_row->layout()->addWidget(button);
        return button;
    };

    QWidget *file_row = make_group(QStringLiteral("File"));
    add_button(file_row, QStringLiteral("New"), this, SLOT(newDocument()));
    add_button(file_row, QStringLiteral("Open"), this, SLOT(openDocument()));
    add_button(file_row, QStringLiteral("Save"), this, SLOT(saveDocument()));
    add_button(file_row, QStringLiteral("Save As"), this, SLOT(saveDocumentAs()));
    add_button(file_row, QStringLiteral("Export"), this, SLOT(exportDocument()));
    add_button(file_row, QStringLiteral("Print"), this, SLOT(printDocument()));

    QWidget *font_row = make_group(QStringLiteral("Font"));
    QFontComboBox *font_family = new QFontComboBox;
    font_family->setToolTip(QStringLiteral("Font family"));
    connect(font_family, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        QTextCharFormat format;
        format.setFontFamilies(QStringList{font.family()});
        mergeFormatOnSelection(format);
    });
    font_row->layout()->addWidget(font_family);

    QSpinBox *font_size = new QSpinBox;
    font_size->setRange(6, 200);
    font_size->setValue(11);
    font_size->setToolTip(QStringLiteral("Font size"));
    connect(font_size, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        QTextCharFormat format;
        format.setFontPointSize(value);
        mergeFormatOnSelection(format);
    });
    font_row->layout()->addWidget(font_size);

    add_toggle(font_row, QStringLiteral("B"), this, SLOT(setBold(bool)));
    add_toggle(font_row, QStringLiteral("I"), this, SLOT(setItalic(bool)));
    add_toggle(font_row, QStringLiteral("U"), this, SLOT(setUnderline(bool)));
    add_toggle(font_row, QStringLiteral("S"), this, SLOT(setStrikethrough(bool)));

    QPushButton *text_color = add_button(font_row, QStringLiteral("A"), this, SLOT(chooseTextColor()));
    text_color->setToolTip(QStringLiteral("Text color"));

    QPushButton *highlight =
        add_button(font_row, QStringLiteral("Highlight"), this, SLOT(chooseHighlightColor()));
    highlight->setToolTip(QStringLiteral("Highlight color"));

    QPushButton *clear_format =
        add_button(font_row, QStringLiteral("Clear"), this, SLOT(clearFormatting()));
    clear_format->setToolTip(QStringLiteral("Clear formatting"));

    QWidget *paragraph_row = make_group(QStringLiteral("Paragraph"));
    add_button(paragraph_row, QStringLiteral("Left"), this, SLOT(setAlignmentLeft()));
    add_button(paragraph_row, QStringLiteral("Center"), this, SLOT(setAlignmentCenter()));
    add_button(paragraph_row, QStringLiteral("Right"), this, SLOT(setAlignmentRight()));
    add_button(paragraph_row, QStringLiteral("Justify"), this, SLOT(setAlignmentJustify()));
    add_button(paragraph_row, QStringLiteral("Bullet list"), this, SLOT(insertBulletList()));
    add_button(paragraph_row, QStringLiteral("Numbered list"), this, SLOT(insertNumberedList()));

    QWidget *heading_row = make_group(QStringLiteral("Heading"));
    QComboBox *heading_selector = new QComboBox;
    heading_selector->addItems(QStringList{QStringLiteral("Body"), QStringLiteral("H1"),
                                           QStringLiteral("H2"), QStringLiteral("H3"),
                                           QStringLiteral("H4"), QStringLiteral("H5"),
                                           QStringLiteral("H6")});
    heading_selector->setToolTip(QStringLiteral("Paragraph style"));
    connect(heading_selector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &WordProcessorModule::setHeading);
    heading_row->layout()->addWidget(heading_selector);

    QWidget *insert_row = make_group(QStringLiteral("Insert"));
    add_button(insert_row, QStringLiteral("Table"), this, SLOT(insertTable()));
    add_button(insert_row, QStringLiteral("Image"), this, SLOT(insertImage()));

    QPushButton *preview_toggle = new QPushButton(QStringLiteral("Preview"));
    preview_toggle->setCheckable(true);
    preview_toggle->setToolTip(QStringLiteral("Page-layout preview"));
    connect(preview_toggle, &QPushButton::toggled, this,
            &WordProcessorModule::togglePageLayoutPreview);
    insert_row->layout()->addWidget(preview_toggle);

    ribbon_scroll->setWidget(ribbon_content);
    return ribbon_scroll;
}

QWidget *WordProcessorModule::buildEditorArea() {
    QSplitter *splitter = new QSplitter(Qt::Horizontal);

    editor_ = new QTextEdit;
    editor_->setAcceptRichText(true);
    editor_->setPlaceholderText(QStringLiteral("Start typing, or open a document to begin editing."));

    QScrollArea *editor_scroll = new QScrollArea;
    editor_scroll->setWidgetResizable(true);
    editor_scroll->setWidget(editor_);
    splitter->addWidget(editor_scroll);

    QScrollArea *preview_scroll = new QScrollArea;
    preview_scroll->setWidgetResizable(true);
    preview_scroll->setFrameShape(QFrame::StyledPanel);
    preview_scroll->setMinimumWidth(220);
    QTextEdit *preview_page = new QTextEdit;
    preview_page->setReadOnly(true);
    preview_page->setFrameShape(QFrame::StyledPanel);
    preview_page->setStyleSheet("QTextEdit { background: palette(base); border: 1px solid palette(mid); }");
    preview_scroll->setWidget(preview_page);
    splitter->addWidget(preview_scroll);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    preview_scroll->setVisible(false);

    connect(editor_, &QTextEdit::textChanged, this, [this, preview_page]() {
        preview_page->setDocument(editor_->document());
    });
    connect(editor_, &QTextEdit::textChanged, this, &WordProcessorModule::markDirty);

    return splitter;
}

QWidget *WordProcessorModule::buildStatusBar() {
    QWidget *status = new QWidget;
    QHBoxLayout *status_layout = new QHBoxLayout(status);
    status_layout->setContentsMargins(12, 4, 12, 4);
    QLabel *status_label = new QLabel(QStringLiteral("Ready"));
    status_layout->addWidget(status_label);
    status_layout->addStretch(1);
    return status;
}

QTextEdit *WordProcessorModule::editor() const { return editor_; }

QString WordProcessorModule::currentFilePath() const { return current_file_path_; }

void WordProcessorModule::newDocument() {
    if (!confirmDiscardChanges()) return;
    editor_->clear();
    current_file_path_.clear();
    dirty_ = false;
}

void WordProcessorModule::openDocument() {
    if (!confirmDiscardChanges()) return;
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open Document"), QString(),
        QStringLiteral("Supported Documents (*.html *.htm *.txt *.md);;All Files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Open Document"),
                             QStringLiteral("Could not open the selected file."));
        return;
    }
    QByteArray content = file.readAll();
    file.close();
    QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".html")) || lower.endsWith(QStringLiteral(".htm"))) {
        editor_->setHtml(QString::fromUtf8(content));
    } else {
        editor_->setPlainText(QString::fromUtf8(content));
    }
    current_file_path_ = path;
    dirty_ = false;
}

bool WordProcessorModule::saveDocument() {
    if (current_file_path_.isEmpty()) return saveDocumentAs();
    QFile file(current_file_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(editor_->toHtml().toUtf8());
    file.close();
    dirty_ = false;
    return true;
}

bool WordProcessorModule::saveDocumentAs() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Document As"), QString(),
        QStringLiteral("HTML Document (*.html);;Plain Text (*.txt);;Markdown (*.md)"));
    if (path.isEmpty()) return false;
    current_file_path_ = path;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".txt")) || lower.endsWith(QStringLiteral(".md"))) {
        file.write(editor_->toPlainText().toUtf8());
    } else {
        file.write(editor_->toHtml().toUtf8());
    }
    file.close();
    dirty_ = false;
    return true;
}

void WordProcessorModule::exportDocument() { saveDocumentAs(); }

void WordProcessorModule::setBold(bool enabled) {
    QTextCharFormat format;
    format.setFontWeight(enabled ? QFont::Bold : QFont::Normal);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::setItalic(bool enabled) {
    QTextCharFormat format;
    format.setFontItalic(enabled);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::setUnderline(bool enabled) {
    QTextCharFormat format;
    format.setFontUnderline(enabled);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::setStrikethrough(bool enabled) {
    QTextCharFormat format;
    format.setFontStrikeOut(enabled);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::chooseTextColor() {
    QColor color = QColorDialog::getColor(editor_->textColor(), this, QStringLiteral("Text Color"));
    if (!color.isValid()) return;
    QTextCharFormat format;
    format.setForeground(color);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::chooseHighlightColor() {
    QColor color = QColorDialog::getColor(Qt::yellow, this, QStringLiteral("Highlight Color"));
    if (!color.isValid()) return;
    QTextCharFormat format;
    format.setBackground(color);
    mergeFormatOnSelection(format);
}

void WordProcessorModule::clearFormatting() {
    QTextCharFormat format;
    format.setFontWeight(QFont::Normal);
    format.setFontItalic(false);
    format.setFontUnderline(false);
    format.setFontStrikeOut(false);
    format.clearForeground();
    format.clearBackground();
    mergeFormatOnSelection(format);
}

void WordProcessorModule::setAlignmentLeft() { editor_->setAlignment(Qt::AlignLeft); }
void WordProcessorModule::setAlignmentCenter() { editor_->setAlignment(Qt::AlignHCenter); }
void WordProcessorModule::setAlignmentRight() { editor_->setAlignment(Qt::AlignRight); }
void WordProcessorModule::setAlignmentJustify() { editor_->setAlignment(Qt::AlignJustify); }

void WordProcessorModule::insertBulletList() {
    QTextCursor cursor = editor_->textCursor();
    cursor.beginEditBlock();
    QTextListFormat list_format;
    list_format.setIndent(1);
    list_format.setStyle(QTextListFormat::ListDisc);
    cursor.createList(list_format);
    cursor.endEditBlock();
}

void WordProcessorModule::insertNumberedList() {
    QTextCursor cursor = editor_->textCursor();
    cursor.beginEditBlock();
    QTextListFormat list_format;
    list_format.setIndent(1);
    list_format.setStyle(QTextListFormat::ListDecimal);
    cursor.createList(list_format);
    cursor.endEditBlock();
}

void WordProcessorModule::setHeading(int level) { applyHeadingStyle(level); }

void WordProcessorModule::applyHeadingStyle(int level) {
    QTextCursor cursor = editor_->textCursor();
    cursor.beginEditBlock();
    QTextBlockFormat block_format;
    QTextCharFormat char_format;
    if (level > 0 && level <= 6) {
        block_format.setHeadingLevel(level);
        int point_size = 24 - (level - 1) * 2;
        char_format.setFontPointSize(point_size);
        char_format.setFontWeight(QFont::Bold);
    } else {
        block_format.setHeadingLevel(0);
        char_format.setFontPointSize(11);
        char_format.setFontWeight(QFont::Normal);
    }
    cursor.mergeBlockFormat(block_format);
    editor_->mergeCurrentCharFormat(char_format);
    cursor.endEditBlock();
}

void WordProcessorModule::insertTable() {
    bool ok = false;
    int rows = QInputDialog::getInt(this, QStringLiteral("Insert Table"),
                                    QStringLiteral("Rows:"), 3, 1, 100, 1, &ok);
    if (!ok) return;
    int columns = QInputDialog::getInt(this, QStringLiteral("Insert Table"),
                                       QStringLiteral("Columns:"), 3, 1, 20, 1, &ok);
    if (!ok) return;
    editor_->textCursor().insertTable(rows, columns);
}

void WordProcessorModule::insertImage() {
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Insert Image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All Files (*)"));
    if (path.isEmpty()) return;
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Insert Image"),
                             QStringLiteral("The selected file is not a supported image."));
        return;
    }
    editor_->textCursor().insertImage(image);
}

void WordProcessorModule::togglePageLayoutPreview(bool enabled) {
    QWidget *parent_widget = editor_->parentWidget();
    while (parent_widget && !qobject_cast<QSplitter *>(parent_widget)) {
        parent_widget = parent_widget->parentWidget();
    }
    if (QSplitter *split = qobject_cast<QSplitter *>(parent_widget)) {
        if (split->count() > 1) split->widget(1)->setVisible(enabled);
    }
}

void WordProcessorModule::printDocument() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    editor_->print(&printer);
}

void WordProcessorModule::mergeFormatOnSelection(const QTextCharFormat &format) {
    QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        editor_->mergeCurrentCharFormat(format);
    } else {
        cursor.mergeCharFormat(format);
        editor_->mergeCurrentCharFormat(format);
    }
}

void WordProcessorModule::markDirty() { dirty_ = true; }

bool WordProcessorModule::confirmDiscardChanges() {
    if (!dirty_) return true;
    QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Unsaved Changes"),
        QStringLiteral("You have unsaved changes. Discard them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveDocument();
    return true;
}

}  // namespace word_processor
}  // namespace me
