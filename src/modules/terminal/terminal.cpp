#include "terminal.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace material_everything {

TerminalModule::TerminalModule(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setDocumentMode(true);
    layout->addWidget(tabs_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this,
            [this](int index) { closeTab(index); });
    addTab();
}

TerminalModule::~TerminalModule() = default;

void TerminalModule::addTab() {
    auto* terminal = new TerminalWidget(settings(), tabs_);
    const int index = tabs_->addTab(terminal,
        tr("Terminal %1").arg(tabs_->count() + 1));
    tabs_->setCurrentIndex(index);
    emit tabCountChanged(tabs_->count());
}

void TerminalModule::closeTab(int index) {
    if (tabs_->count() <= 0 || index < 0 || index >= tabs_->count()) return;
    auto* widget = qobject_cast<TerminalWidget*>(tabs_->widget(index));
    if (widget) {
        widget->stop();
        widget->deleteLater();
    }
    tabs_->removeTab(index);
    if (tabs_->count() == 0) {
        addTab();
    } else {
        emit tabCountChanged(tabs_->count());
    }
}

int TerminalModule::tabCount() const {
    return tabs_ ? tabs_->count() : 0;
}

void TerminalModule::applySettings(const TerminalSettings& next) {
    settings_ = next;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (auto* terminal = qobject_cast<TerminalWidget*>(tabs_->widget(i))) {
            terminal->applySettings(next);
        }
    }
}

}  // namespace material_everything
