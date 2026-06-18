#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("AiboxSearchTool");
    QApplication::setOrganizationName("cwai");

    MainWindow window;
    window.show();

    return QApplication::exec();
}
