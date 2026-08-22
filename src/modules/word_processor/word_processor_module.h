#pragma once

#include <QMainWindow>
#include <QString>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QTextListFormat>
#include <QWidget>

namespace me {
namespace word_processor {

class WordProcessorModule final : public QWidget {
    Q_OBJECT
   public:
    explicit WordProcessorModule(QWidget *parent = nullptr);
    ~WordProcessorModule() override = default;

    QTextEdit *editor() const;
    QString currentFilePath() const;

   public slots:
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    void exportDocument();

    void setBold(bool enabled);
    void setItalic(bool enabled);
    void setUnderline(bool enabled);
    void setStrikethrough(bool enabled);
    void chooseTextColor();
    void chooseHighlightColor();
    void clearFormatting();

    void setAlignmentLeft();
    void setAlignmentCenter();
    void setAlignmentRight();
    void setAlignmentJustify();

    void insertBulletList();
    void insertNumberedList();

    void setHeading(int level);  // 0 = body text, 1..6 = heading levels
    void insertTable();
    void insertImage();
    void togglePageLayoutPreview(bool enabled);
    void printDocument();

   private:
    void buildUi();
    QWidget *buildRibbon();
    QWidget *buildEditorArea();
    QWidget *buildStatusBar();

    void mergeFormatOnSelection(const QTextCharFormat &format);
    void applyHeadingStyle(int level);
    void updateFormatActions();
    void markDirty();
    bool confirmDiscardChanges();

    QTextEdit *editor_;
    QString current_file_path_;
    bool dirty_ = false;
};

}  // namespace word_processor
}  // namespace me
