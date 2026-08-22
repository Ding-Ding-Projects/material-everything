#pragma once

#include <QDir>
#include <limits>
#include <QPlainTextEdit>
#include <QProcess>
#include <QString>

class QTextCursor;

namespace material_everything {

struct TerminalSettings;

class TerminalWidget : public QPlainTextEdit {
    Q_OBJECT
public:
    TerminalWidget(const struct TerminalSettings& settings, QWidget* parent);
    ~TerminalWidget() override;

    void stop();
    void applySettings(const TerminalSettings& settings);
    bool isRunning() const { return process_ && process_->state() == QProcess::Running; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void readStdout();
    void readStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void launchShell();
    void appendOutput(const QString& text);
    void sendToShell(const QString& text);
    void pasteFromClipboard();

    class QProcess* process_ = nullptr;
    QString pendingLine_;
    TerminalSettings settings_;
    int scrollbackLimit_ = 10000;
};

}  // namespace material_everything
