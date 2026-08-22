#include "terminal_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QProcess>

#include "terminal.h"

namespace material_everything {

TerminalWidget::TerminalWidget(const TerminalSettings& settings, QWidget* parent)
    : QPlainTextEdit(parent), settings_(settings) {
    setReadOnly(true);
    setMaximumBlockCount(scrollbackLimit_);
    applySettings(settings_);
    launchShell();
}

TerminalWidget::~TerminalWidget() {
    stop();
}

void TerminalWidget::stop() {
    if (!process_) return;
    process_->disconnect(this);
    if (process_->state() == QProcess::Running) {
        process_->terminate();
        if (!process_->waitForFinished(2000)) {
            process_->kill();
            process_->waitForFinished(1000);
        }
    }
}

void TerminalWidget::applySettings(const TerminalSettings& next) {
    settings_ = next;
    QFont font(settings_.fontFamily, settings_.fontSizePx);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, settings_.backgroundColor);
    pal.setColor(QPalette::Text, settings_.foregroundColor);
    setPalette(pal);
    viewport()->update();
}

void TerminalWidget::launchShell() {
    process_ = new QProcess(this);
#ifdef _WIN32
    const QString program = QStringLiteral("powershell.exe");
    QStringList arguments;
    arguments << QStringLiteral("-NoLogo");
#else
    const QString program = QStringLiteral("bash");
    QStringList arguments;
    arguments << QStringLiteral("--noprofile") << QStringLiteral("--norc")
              << QStringLiteral("-i");
#endif
    connect(process_, &QProcess::readyReadStandardOutput, this,
            &TerminalWidget::readStdout);
    connect(process_, &QProcess::readyReadStandardError, this,
            &TerminalWidget::readStderr);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalWidget::onProcessFinished);
    process_->setWorkingDirectory(QDir::homePath());
    process_->start(program, arguments);
    appendOutput(tr("Starting %1...\r\n").arg(program));
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
    // Copy/paste support: Ctrl+C copies when text is selected, otherwise sends SIGINT-equivalent.
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_C && textCursor().hasSelection()) {
            copy();
            return;
        }
        if (event->key() == Qt::Key_V) {
            pasteFromClipboard();
            return;
        }
    }
    // Route printable characters and Enter to the shell.
    QString text = event->text();
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        sendToShell(pendingLine_ + QStringLiteral("\n"));
        pendingLine_.clear();
        return;
    }
    if (event->key() == Qt::Key_Backspace) {
        if (!pendingLine_.isEmpty()) {
            pendingLine_.chop(1);
            QTextCursor c = textCursor();
            c.deletePreviousChar();
        }
        return;
    }
    if (!text.isEmpty() && event->key() != Qt::Key_unknown && !text.startsWith(QLatin1Char('\x7f'))) {
        pendingLine_ += text;
        QTextCursor cursor = textCursor();
        cursor.insertText(text);
        setTextCursor(cursor);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void TerminalWidget::pasteFromClipboard() {
    const QString clipboard = QApplication::clipboard()->text();
    if (!clipboard.isEmpty()) {
        sendToShell(clipboard);
        pendingLine_ += clipboard;
    }
}

void TerminalWidget::sendToShell(const QString& text) {
    if (isRunning()) {
        process_->write(text.toUtf8());
    }
}

void TerminalWidget::readStdout() {
    appendOutput(QString::fromLocal8Bit(process_->readAllStandardOutput()));
}

void TerminalWidget::readStderr() {
    appendOutput(QString::fromLocal8Bit(process_->readAllStandardError()));
}

void TerminalWidget::appendOutput(const QString& text) {
    QTextCursor cursor = document()->findBlockByNumber(std::numeric_limits<int>::max());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void TerminalWidget::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    QString message;
    if (status == QProcess::CrashExit) {
        message = tr("\r\n[Process crashed]\r\n");
    } else {
        message = tr("\r\n[Process exited with code %1]\r\n").arg(exitCode);
    }
    appendOutput(message);
}

}  // namespace material_everything
