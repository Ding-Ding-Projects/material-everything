#pragma once

#include <QProcess>
#include <QColor>
#include <QString>
#include <QWidget>

class QTabWidget;

namespace material_everything {

class TerminalWidget;

struct TerminalSettings {
    QString fontFamily = QStringLiteral("Consolas");
    int fontSizePx = 13;
    QColor backgroundColor = QColor(0x1E, 0x1B, 0x16);
    QColor foregroundColor = QColor(0xE6, 0xE1, 0xE5);
};

class TerminalModule : public QWidget {
    Q_OBJECT
public:
    explicit TerminalModule(QWidget* parent = nullptr);
    ~TerminalModule() override;

    void addTab();
    void closeTab(int index);
    int tabCount() const;

    const TerminalSettings& settings() const { return settings_; }
    void applySettings(const TerminalSettings& next);

signals:
    void tabCountChanged(int count);

private:
    class QTabWidget* tabs_;
    TerminalSettings settings_;
};

}  // namespace material_everything
