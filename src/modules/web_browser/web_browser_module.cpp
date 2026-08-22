#include "web_browser_module.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace material_everything {
namespace {

constexpr auto kSurfaceName = "MaterialWebBrowser";
constexpr auto kM3Background = "#1c1b1f";
constexpr auto kM3Container = "#4a4458";
constexpr auto kM3OnContainer = "#e8def8";
constexpr auto kM3Primary = "#d0bcff";

QString normalizeUrl(const QString& input) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("about:blank");
    }
    if (trimmed.startsWith("http://") || trimmed.startsWith("https://") ||
        trimmed.startsWith("about:") || trimmed.contains(' ') || trimmed.contains('.')) {
        return trimmed;
    }
    return QStringLiteral("https://duckduckgo.com/?q=") + QString::fromUtf8(
        QUrl::toPercentEncoding(trimmed));
}

class EnginePage : public QWidget {
public:
    explicit EnginePage(QWidget* parent) : QWidget(parent), address_(this) {}

    void setUrl(const QUrl& value) { currentUrl_ = value; }
    QUrl url() const { return currentUrl_; }
    void setTitle(const QString& value) { title_ = value; }
    QString title() const { return title_; }
    bool canBack() const { return false; }
    bool canForward() const { return false; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(kM3Background));
        painter.setPen(QColor(kM3OnContainer));
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter,
                         "WebView backend is unavailable.\nNavigation state remains available.");
    }

private:
    QLineEdit& address_;
    QUrl currentUrl_;
    QString title_;
};

}  // namespace

class WebBrowserModule::Implementation {
public:
    explicit Implementation(QWidget* chromeHost) {
        surface = new QWidget(chromeHost);
        surface->setObjectName(kSurfaceName);
        surface->setStyleSheet(QString(R"(
            QWidget#%1 { background: %2; }
            QLineEdit { background: %3; color: %4; border-radius: 18px; padding: 6px 16px; }
            QToolButton { background: transparent; color: %4; border-radius: 14px; min-width: 28px; }
            QToolButton:hover { background: rgba(208,188,255,.12); }
            QToolButton:disabled { color: rgba(232,222,248,.38); }
            QTabWidget::pane { border: none; }
            QTabBar::tab { background: %3; color: %4; padding: 7px 14px; border-radius: 12px; margin-right: 5px; }
            QTabBar::tab:selected { background: %3; }
        )").arg(kSurfaceName, kM3Background, kM3Container, kM3OnContainer));

        auto layout = new QVBoxLayout(surface);
        layout->setContentsMargins(10, 8, 10, 10);
        layout->setSpacing(8);

        auto controls = new QHBoxLayout;
        controls->setSpacing(5);
        backButton = createButton("\u2039", "Back");
        forwardButton = createButton("\u203a", "Forward");
        reloadButton = createButton("\u21bb", "Reload");
        addressEdit = new QLineEdit(surface);
        addressEdit->setPlaceholderText("Search or enter web address");
        newTabButton = createButton("+", "New browser tab");
        bookmarkButton = createButton("\u2605", "Bookmark this page");

        tabs = new QTabWidget(surface);
        tabs->setDocumentMode(true);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);

        controls->addWidget(backButton);
        controls->addWidget(forwardButton);
        controls->addWidget(reloadButton);
        controls->addWidget(addressEdit, 1);
        controls->addWidget(bookmarkButton);
        controls->addWidget(newTabButton);
        layout->addLayout(controls);
        layout->addWidget(tabs, 1);

        statusLabel = new QLabel(surface);
        statusLabel->setStyleSheet("color:#cac4d0;");
        layout->addWidget(statusLabel);

        connect(addressEdit, &QLineEdit::returnPressed, [this] {
            owner->navigate(addressEdit->text());
        });
        connect(backButton, &QAbstractButton::clicked, [this] { owner->back(); });
        connect(forwardButton, &QAbstractButton::clicked, [this] { owner->forward(); });
        connect(reloadButton, &QAbstractButton::clicked, [this] { owner->reload(); });
        connect(newTabButton, &QAbstractButton::clicked, [this] { addPage(); });
        connect(tabs, &QTabWidget::currentChanged, [this](int index) { syncCurrent(index); });
        connect(bookmarkButton, &QAbstractButton::clicked, [this] {
            if (auto page = currentPage()) {
                owner->addBookmark(page->title().isEmpty() ? page->url().toString()
                                                           : page->title(),
                                   page->url().toString());
            }
        });
    }

    static QToolButton* createButton(const QString& text, const QString& name) {
        auto button = new QToolButton(nullptr);
        button->setText(text);
        button->setAccessibleName(name);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    }

    EnginePage* addPage() {
        auto page = new EnginePage(tabs);
        pages.append(page);
        tabs->addTab(page, "New tab");
        tabs->setCurrentWidget(page);
        return page;
    }

    EnginePage* currentPage() const { return qobject_cast<EnginePage*>(tabs->currentWidget()); }

    void syncCurrent(int) {
        if (!owner || !currentPage()) return;
        emitNavigation(*currentPage(), false);
    }

    void emitNavigation(const EnginePage& page, bool loading) {
        emit owner->navigationChanged(page.url().toString(), page.title(), page.canBack(),
                                      page.canForward(), loading);
    }

    QWidget* surface = nullptr;
    QWidget* chromeHost = nullptr;
    QTabWidget* tabs = nullptr;
    QLineEdit* addressEdit = nullptr;
    QToolButton* backButton = nullptr;
    QToolButton* forwardButton = nullptr;
    QToolButton* reloadButton = nullptr;
    QToolButton* newTabButton = nullptr;
    QToolButton* bookmarkButton = nullptr;
    QLabel* statusLabel = nullptr;
    QList<EnginePage*> pages;
    WebBrowserModule* owner = nullptr;
};

WebBrowserModule::WebBrowserModule(QWidget* chromeHost, QObject* parent)
    : QObject(parent), impl_(new Implementation(chromeHost)) {
    impl_->owner = this;
    impl_->addPage();
}

WebBrowserModule::~WebBrowserModule() { delete impl_; }

QWidget* WebBrowserModule::surface() const { return impl_->surface; }

void WebBrowserModule::navigate(const QString& input) {
    if (!impl_->currentPage()) impl_->addPage();
    auto page = impl_->currentPage();
    const QString normalized = normalizeUrl(input);
    page->setUrl(normalized);
    page->setTitle(normalized);
    impl_->addressEdit->setText(normalized);
    const int index = impl_->pages.indexOf(page);
    impl_->tabs->setTabText(index, normalized);
    history_.append({normalized, normalized, QDateTime::currentDateTime()});
    emit historyChanged(history_);
    emit navigationChanged(normalized, normalized, page->canBack(), page->canForward(), false);
    emit loadProgress(100);
}

void WebBrowserModule::back() { emit loadProgress(0); }
void WebBrowserModule::forward() { emit loadProgress(0); }
void WebBrowserModule::reload() { emit loadProgress(0); }
void WebBrowserModule::stopLoading() { emit loadProgress(0); }

void WebBrowserModule::addBookmark(const QString& title, const QString& url) {
    for (const auto& existing : bookmarks_) {
        if (existing.url == url) return;
    }
    bookmarks_.append({title.isEmpty() ? url : title, url});
    emit bookmarksChanged(bookmarks_);
}

void WebBrowserModule::removeBookmark(const QString& url) {
    for (int index = 0; index < bookmarks_.size(); ++index) {
        if (bookmarks_[index].url == url) {
            bookmarks_.removeAt(index);
            break;
        }
    }
    emit bookmarksChanged(bookmarks_);
}

const QList<WebBookmark>& WebBrowserModule::bookmarks() const { return bookmarks_; }
const QList<WebHistoryEntry>& WebBrowserModule::history() const { return history_; }
const QList<WebDownload>& WebBrowserModule::downloads() const { return downloads_; }
