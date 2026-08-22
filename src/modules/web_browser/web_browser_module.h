#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class WebBrowserEngine;
class QWidget;

namespace material_everything {

struct WebBookmark {
    QString title;
    QString url;
};

struct WebHistoryEntry {
    QString title;
    QString url;
    QDateTime visitedAt;
};

struct WebDownload {
    int id = 0;
    QString url;
    QString destinationPath;
    qint64 receivedBytes = 0;
    qint64 totalBytes = 0;
    bool finished = false;
};

class WebBrowserModule : public QObject {
    Q_OBJECT
public:
    explicit WebBrowserModule(QWidget* chromeHost, QObject* parent = nullptr);
    ~WebBrowserModule() override;

    QWidget* surface() const;
    void navigate(const QString& input);
    void back();
    void forward();
    void reload();
    void stopLoading();
    void addBookmark(const QString& title, const QString& url);
    void removeBookmark(const QString& url);
    const QList<WebBookmark>& bookmarks() const;
    const QList<WebHistoryEntry>& history() const;
    const QList<WebDownload>& downloads() const;

private:
    QList<WebBookmark> bookmarks_;
    QList<WebHistoryEntry> history_;
    QList<WebDownload> downloads_;

signals:
    void navigationChanged(const QString& url, const QString& title, bool canGoBack,
                           bool canGoForward, bool loading);
    void loadProgress(int percent);
    void bookmarksChanged(const QList<WebBookmark>& bookmarks);
    void historyChanged(const QList<WebHistoryEntry>& entries);
    void downloadProgress(const WebDownload& download);

private:
    class Implementation;
    Implementation* impl_;
};

}  // namespace material_everything
