#include <QApplication>

#include "word_processor_module.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Material Everything — Word Processor"));
    me::word_processor::WordProcessorModule module;
    module.resize(1100, 760);
    module.show();
    return app.exec();
}
