#include "MainWindow.h"

#include <QApplication>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	QApplication::setOrganizationName("DevicesForge");
	QApplication::setApplicationName("Capturador");
	QApplication::setApplicationVersion("1.0.0");

	MainWindow window;
	window.show();
	return app.exec();
}
