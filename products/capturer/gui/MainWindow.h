#pragma once

#include <QMainWindow>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class CaptureJob;
class ResponsePlot;

/**
 * MainWindow
 *
 * Ventana del capturador: generar el barrido y analizar la grabacion.
 */
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

private:
	void buildUi();
	void restoreSettings();
	void saveSettings() const;
	void setBusy(bool busy);
	QString chooseOpen(const QString& title, const QString& filter, const QString& current);
	QString chooseSave(const QString& title, const QString& filter, const QString& current, const QString& fallback);

	void onGenerate();
	void onAnalyze();
	void onJobFinished();
	void applyCaptureName();

	QComboBox* rateBox { nullptr };
	QDoubleSpinBox* durationBox { nullptr };
	QSpinBox* repeatsBox { nullptr };
	QLineEdit* sweepPath { nullptr };
	QLineEdit* recordingPath { nullptr };
	QLineEdit* nameEdit { nullptr };
	QLineEdit* deviceEdit { nullptr };
	QLineEdit* authorEdit { nullptr };
	QLineEdit* notesEdit { nullptr };
	QDoubleSpinBox* sendBox { nullptr };
	QLineEdit* capturePath { nullptr };
	QPlainTextEdit* report { nullptr };
	ResponsePlot* plot { nullptr };
	QPushButton* generateButton { nullptr };
	QPushButton* analyzeButton { nullptr };
	QLabel* statusLabel { nullptr };

	CaptureJob* job { nullptr };
	QString autoCaptureName;
};
