#include "MainWindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    erebus::MainWindow window;
    window.show();
    return application.exec();
}
