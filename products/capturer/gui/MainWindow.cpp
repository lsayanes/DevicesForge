#include "MainWindow.h"

#include "OfflineCapturer.h"
#include "ResponsePlot.h"
#include "WavIO.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>

#include <fstream>

namespace
{

	constexpr char kStyle[] = R"(
		QWidget { background: #1c1e22; color: #dcdde2; font-size: 13px; }
		QGroupBox {
			border: 1px solid #3a3e44; border-radius: 6px;
			margin-top: 8px; padding: 8px 8px 6px 8px; font-weight: 600;
		}
		QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #7dccc4; }
		QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
			background: #121416; color: #dcdde2;
			border: 1px solid #4a5058; border-radius: 4px; padding: 4px 6px; min-height: 22px;
		}
		QPlainTextEdit { font-family: Menlo, monospace; font-size: 12px; }
		QPushButton { min-height: 26px; padding: 2px 12px; border-radius: 4px; background: #2c3138; color: #dcdde2; }
		QPushButton:disabled { color: #6a7078; background: #24282e; }
		QPushButton#generate { background: #e69440; color: #1c1e22; font-weight: 600; }
		QPushButton#analyze { background: #3c9e94; color: #10201e; font-weight: 600; }
		QLabel#hint { color: #c4c8d0; font-weight: 400; }
		QLabel#status { font-weight: 600; }
	)";

	QString documentsDir()
	{
		const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
		return dir.isEmpty() ? QDir::homePath() : dir;
	}

	QString qualityPathFor(const QString& capturePath)
	{
		const QFileInfo info(capturePath);
		return info.dir().filePath(info.completeBaseName() + ".quality.txt");
	}

	/*
	 * sanitizedCaptureFileName
	 *
	 * Pseudocodigo:
	 * 1. Sacar caracteres que no entran en un nombre de archivo
	 * 2. Agregar la extension si falta
	 */
	QString sanitizedCaptureFileName(const QString& name)
	{
		QString file = name.trimmed();
		for (QChar& ch : file)
		{
			if (ch.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(ch))
				ch = QLatin1Char('-');
		}
		while (file.endsWith(QLatin1Char('.')) || file.endsWith(QLatin1Char(' ')))
			file.chop(1);
		if (file.isEmpty())
			return {};
		if (!file.endsWith(QStringLiteral(".dfcapture"), Qt::CaseInsensitive))
			file += QStringLiteral(".dfcapture");
		return file;
	}

} // namespace

/*
 * CaptureJob
 *
 * Pseudocodigo:
 * 1. Si es barrido, escribir el WAV
 * 2. Si es analisis, leer la grabacion, analizar y guardar la captura
 */
class CaptureJob : public QThread
{
	public:
		enum class Kind
		{
			Sweep,
			Analyze
		};

		Kind kind { Kind::Sweep };
		double sampleRate { 48000.0 };
		float duration { 3.0f };
		int repeats { 3 };
		float sendDb { 0.0f };
		QString sweepFile;
		QString recordingFile;
		QString captureFile;
		QString name;
		QString device;
		QString author;
		QString notes;

		bool ok { false };
		QString summary;
		std::vector<float> bandHz;
		std::vector<std::vector<float>> passBandsDb;
		std::vector<float> irBandsDb;

	protected:
		void run() override
		{
			ok = false;
			summary.clear();
			bandHz.clear();
			passBandsDb.clear();
			irBandsDb.clear();
			if (kind == Kind::Sweep)
				writeSweep();
			else
				writeCapture();
		}

	private:
		void writeSweep()
		{
			DevicesForge::SweepSpec spec;
			spec.sampleRate = sampleRate;
			spec.durationSeconds = duration;
			spec.repeats = repeats;

			std::vector<float> samples;
			DevicesForge::SweepLayout layout;
			if (!DevicesForge::OfflineCapturer::makeSweep(spec, samples, layout))
			{
				summary = "No pude armar el barrido. Duración entre 0,5 y 5 s, repeticiones de 1 a 8.";
				return;
			}

			DevicesForge::WavBuffer wav;
			wav.sampleRate = sampleRate;
			wav.channels.assign(1, std::move(samples));
			if (!DevicesForge::WavIO::save(sweepFile.toStdString(), wav, 32))
			{
				summary = "No pude escribir " + sweepFile;
				return;
			}

			summary = QString("Barrido listo: %1\n%2 Hz, %3 s, %4 repeticiones, %5 muestras.")
						  .arg(sweepFile)
						  .arg(sampleRate, 0, 'f', 0)
						  .arg(duration, 0, 'f', 1)
						  .arg(repeats)
						  .arg(wav.channels[0].size());
			ok = true;
		}

		void writeCapture()
		{
			DevicesForge::WavBuffer wav;
			if (!DevicesForge::WavIO::load(recordingFile.toStdString(), wav) || wav.channels.empty() ||
				wav.channels[0].empty())
			{
				summary = "No pude leer " + recordingFile;
				return;
			}

			DevicesForge::AnalyzeOptions options;
			options.sweep.durationSeconds = duration;
			options.sweep.repeats = repeats;
			options.name = name.toStdString();
			options.device = device.toStdString();
			options.author = author.toStdString();
			options.notes = notes.toStdString();
			options.sendDb = sendDb;

			DevicesForge::CaptureData capture;
			DevicesForge::QualityReport report;
			const std::vector<float>& channel = wav.channels[0];
			if (!DevicesForge::OfflineCapturer::analyze(channel.data(), static_cast<int32_t>(channel.size()),
														wav.sampleRate, options, capture, report))
			{
				summary = QString::fromStdString(report.text);
				if (summary.isEmpty())
					summary = "El análisis falló.";
				return;
			}

			if (capture.name.empty())
				capture.name = QFileInfo(captureFile).completeBaseName().toStdString();

			if (!DevicesForge::CaptureFile::save(captureFile.toStdString(), capture))
			{
				summary = "No pude escribir " + captureFile;
				return;
			}

			const QString qualityPath = qualityPathFor(captureFile);
			std::ofstream quality(qualityPath.toStdString());
			quality << report.text;
			if (!quality)
			{
				summary = "La captura se guardó, pero no pude escribir " + qualityPath;
				return;
			}

			const std::string htmlPath = DevicesForge::reportHtmlPath(captureFile.toStdString());
			if (!DevicesForge::OfflineCapturer::writeHtmlReport(htmlPath, capture, report))
			{
				summary = "La captura se guardó, pero no pude escribir " + QString::fromStdString(htmlPath);
				return;
			}

			summary = QString::fromStdString(report.text) + "informe=" + QString::fromStdString(htmlPath);
			bandHz = std::move(report.bandHz);
			passBandsDb = std::move(report.passBandsDb);
			irBandsDb = std::move(report.irBandsDb);
			ok = true;
		}
};

namespace
{

	QWidget* pathRow(QLineEdit*& edit, QPushButton*& button, const QString& buttonText)
	{
		edit = new QLineEdit;
		button = new QPushButton(buttonText);
		auto* row = new QWidget;
		auto* layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->addWidget(edit, 1);
		layout->addWidget(button);
		return row;
	}

	QWidget* labeled(const QString& text, QWidget* field, int stretch = 0)
	{
		auto* row = new QWidget;
		auto* layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(6);
		layout->addWidget(new QLabel(text));
		layout->addWidget(field, stretch);
		return row;
	}

} // namespace

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
{
	job = new CaptureJob;
	connect(job, &QThread::finished, this, &MainWindow::onJobFinished);
	buildUi();
	restoreSettings();
	resize(980, 700);
	/* Si macOS restaura el alto viejo, lo bajo a la pantalla mas chica (la de la Mac). */
	QTimer::singleShot(0, this, [this] {
		int shortest = height();
		const auto screens = QGuiApplication::screens();
		for (const QScreen* screen : screens)
			shortest = qMin(shortest, screen->availableGeometry().height() - 24);
		if (height() > shortest)
			resize(width(), shortest);
	});
}

MainWindow::~MainWindow()
{
	if (job->isRunning())
		job->wait();
	delete job;
}

void MainWindow::buildUi()
{
	setWindowTitle("Capturador");
	setStyleSheet(kStyle);

	auto* central = new QWidget;
	auto* root = new QVBoxLayout(central);
	root->setContentsMargins(12, 8, 12, 8);
	root->setSpacing(4);

	auto* title = new QLabel("Capturador");
	title->setStyleSheet("font-size: 18px; font-weight: 600; color: #f0f1f4;");
	root->addWidget(title);

	auto* sweepGroup = new QGroupBox("Barrido");
	auto* sweepLayout = new QVBoxLayout(sweepGroup);
	sweepLayout->setSpacing(4);
	rateBox = new QComboBox;
	rateBox->addItem("44100 Hz", 44100.0);
	rateBox->addItem("48000 Hz", 48000.0);
	rateBox->addItem("88200 Hz", 88200.0);
	rateBox->addItem("96000 Hz", 96000.0);
	rateBox->setCurrentIndex(1);
	durationBox = new QDoubleSpinBox;
	durationBox->setRange(0.5, 5.0);
	durationBox->setSingleStep(0.5);
	durationBox->setDecimals(1);
	durationBox->setValue(3.0);
	durationBox->setSuffix(" s");
	durationBox->setMaximumWidth(100);
	repeatsBox = new QSpinBox;
	repeatsBox->setRange(1, 8);
	repeatsBox->setValue(3);
	repeatsBox->setMaximumWidth(70);
	QPushButton* sweepBrowse = nullptr;
	auto* sweepRow = pathRow(sweepPath, sweepBrowse, "Elegir…");
	generateButton = new QPushButton("Generar barrido");
	generateButton->setObjectName("generate");

	auto* params = new QHBoxLayout;
	params->setSpacing(10);
	params->addWidget(labeled("Tasa", rateBox));
	params->addWidget(labeled("Duración", durationBox));
	params->addWidget(labeled("Repeticiones", repeatsBox));
	params->addStretch(1);
	params->addWidget(generateButton);
	sweepLayout->addLayout(params);
	sweepLayout->addWidget(labeled("Archivo", sweepRow, 1));
	auto* sweepHint = new QLabel("Las repeticiones se promedian. Entre pasadas hay 0,5 s de cola.");
	sweepHint->setObjectName("hint");
	sweepLayout->addWidget(sweepHint);
	root->addWidget(sweepGroup);

	auto* analyzeGroup = new QGroupBox("Análisis");
	auto* analyzeLayout = new QVBoxLayout(analyzeGroup);
	analyzeLayout->setSpacing(4);
	QPushButton* recordingBrowse = nullptr;
	QPushButton* captureBrowse = nullptr;
	auto* recordingRow = pathRow(recordingPath, recordingBrowse, "Elegir…");
	nameEdit = new QLineEdit;
	nameEdit->setPlaceholderText("BOSS CS-3");
	deviceEdit = new QLineEdit;
	deviceEdit->setPlaceholderText("pedal, canal, cinta");
	authorEdit = new QLineEdit;
	notesEdit = new QLineEdit;
	notesEdit->setPlaceholderText("opcional");
	sendBox = new QDoubleSpinBox;
	sendBox->setRange(-24.0, 24.0);
	sendBox->setSingleStep(1.0);
	sendBox->setDecimals(1);
	sendBox->setSuffix(" dB");
	sendBox->setMaximumWidth(100);
	auto* captureRow = pathRow(capturePath, captureBrowse, "Elegir…");
	analyzeButton = new QPushButton("Analizar");
	analyzeButton->setObjectName("analyze");
	report = new QPlainTextEdit;
	report->setReadOnly(true);
	report->setPlaceholderText("Acá aparece el reporte de la toma.");
	report->setMinimumHeight(200);
	plot = new ResponsePlot;

	analyzeLayout->addWidget(labeled("Grabación", recordingRow, 1));
	auto* meta = new QHBoxLayout;
	meta->setSpacing(10);
	meta->addWidget(labeled("Nombre", nameEdit, 1));
	meta->addWidget(labeled("Equipo", deviceEdit, 1));
	meta->addWidget(labeled("Autor", authorEdit, 1));
	analyzeLayout->addLayout(meta);
	auto* extra = new QHBoxLayout;
	extra->setSpacing(10);
	extra->addWidget(labeled("Notas", notesEdit, 1));
	extra->addWidget(labeled("Envío", sendBox));
	analyzeLayout->addLayout(extra);
	auto* captureLine = new QHBoxLayout;
	captureLine->setSpacing(10);
	captureLine->addWidget(labeled("Captura", captureRow, 1));
	captureLine->addWidget(analyzeButton);
	analyzeLayout->addLayout(captureLine);
	auto* analyzeHint = new QLabel(
		"Misma duración y repeticiones que el barrido. La tasa sale del WAV. "
		"Se corrige la velocidad media, no el wow. "
		"Si el archivo sigue siendo captura, el nombre pasa a ser el archivo. "
		"Al lado de la captura queda un informe HTML.");
	analyzeHint->setObjectName("hint");
	analyzeLayout->addWidget(analyzeHint);
	auto* results = new QHBoxLayout;
	results->setSpacing(10);
	results->addWidget(report, 2);
	results->addWidget(plot, 3);
	analyzeLayout->addLayout(results, 1);
	root->addWidget(analyzeGroup);

	statusLabel = new QLabel("Listo.");
	statusLabel->setObjectName("status");
	root->addWidget(statusLabel);

	setCentralWidget(central);

	connect(sweepBrowse, &QPushButton::clicked, this, [this] {
		const QString path = chooseSave("Guardar barrido", "WAV (*.wav)", sweepPath->text(), "sweep.wav");
		if (!path.isEmpty())
			sweepPath->setText(path);
	});
	connect(recordingBrowse, &QPushButton::clicked, this, [this] {
		const QString path = chooseOpen("Elegir grabación", "WAV (*.wav)", recordingPath->text());
		if (!path.isEmpty())
			recordingPath->setText(path);
	});
	connect(captureBrowse, &QPushButton::clicked, this, [this] {
		const QString path = chooseSave("Guardar captura", "Captura (*.dfcapture)", capturePath->text(),
										"captura.dfcapture");
		if (!path.isEmpty())
			capturePath->setText(path);
	});
	connect(generateButton, &QPushButton::clicked, this, &MainWindow::onGenerate);
	connect(analyzeButton, &QPushButton::clicked, this, &MainWindow::onAnalyze);
	connect(nameEdit, &QLineEdit::textChanged, this, [this] { applyCaptureName(); });
	connect(durationBox, &QDoubleSpinBox::valueChanged, this, [this] { saveSettings(); });
	connect(repeatsBox, &QSpinBox::valueChanged, this, [this] { saveSettings(); });
	connect(rateBox, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });
}

void MainWindow::restoreSettings()
{
	QSettings settings;
	const double rate = settings.value("rate", 48000.0).toDouble();
	const int rateIndex = rateBox->findData(rate);
	if (rateIndex >= 0)
		rateBox->setCurrentIndex(rateIndex);
	durationBox->setValue(settings.value("duration", 3.0).toDouble());
	repeatsBox->setValue(settings.value("repeats", 3).toInt());
	sweepPath->setText(settings.value("sweepPath").toString());
	recordingPath->setText(settings.value("recordingPath").toString());
	capturePath->setText(settings.value("capturePath").toString());
	nameEdit->setText(settings.value("name").toString());
	deviceEdit->setText(settings.value("device").toString());
	authorEdit->setText(settings.value("author").toString());
	notesEdit->setText(settings.value("notes").toString());
	sendBox->setValue(settings.value("sendDb", 0.0).toDouble());
	applyCaptureName();
}

void MainWindow::saveSettings() const
{
	QSettings settings;
	settings.setValue("rate", rateBox->currentData().toDouble());
	settings.setValue("duration", durationBox->value());
	settings.setValue("repeats", repeatsBox->value());
	settings.setValue("sweepPath", sweepPath->text());
	settings.setValue("recordingPath", recordingPath->text());
	settings.setValue("capturePath", capturePath->text());
	settings.setValue("name", nameEdit->text());
	settings.setValue("device", deviceEdit->text());
	settings.setValue("author", authorEdit->text());
	settings.setValue("notes", notesEdit->text());
	settings.setValue("sendDb", sendBox->value());
}

void MainWindow::setBusy(bool busy)
{
	generateButton->setEnabled(!busy);
	analyzeButton->setEnabled(!busy);
	if (busy)
		statusLabel->setText(job->kind == CaptureJob::Kind::Sweep ? "Generando barrido…" : "Analizando…");
}

QString MainWindow::chooseOpen(const QString& title, const QString& filter, const QString& current)
{
	const QString start = current.isEmpty() ? documentsDir() : current;
	return QFileDialog::getOpenFileName(this, title, start, filter);
}

QString MainWindow::chooseSave(const QString& title, const QString& filter, const QString& current,
							   const QString& fallback)
{
	QString start = current;
	if (start.isEmpty())
		start = documentsDir() + "/" + fallback;
	return QFileDialog::getSaveFileName(this, title, start, filter);
}

void MainWindow::onGenerate()
{
	if (job->isRunning())
		return;
	saveSettings();
	if (sweepPath->text().trimmed().isEmpty())
	{
		statusLabel->setText("Elegí dónde guardar el barrido.");
		return;
	}

	job->kind = CaptureJob::Kind::Sweep;
	job->sampleRate = rateBox->currentData().toDouble();
	job->duration = static_cast<float>(durationBox->value());
	job->repeats = repeatsBox->value();
	job->sweepFile = sweepPath->text().trimmed();
	setBusy(true);
	job->start();
}

/*
 * applyCaptureName
 *
 * Pseudocodigo:
 * 1. Si el archivo es captura o el que puso el nombre, usar el nombre
 * 2. Si el usuario eligio otro archivo, dejarlo
 */
void MainWindow::applyCaptureName()
{
	const QString file = sanitizedCaptureFileName(nameEdit->text());
	const QString current = capturePath->text().trimmed();
	const QString base = QFileInfo(current).fileName();
	const bool generic = current.isEmpty() ||
						 base.compare(QStringLiteral("captura.dfcapture"), Qt::CaseInsensitive) == 0 ||
						 (!autoCaptureName.isEmpty() && base.compare(autoCaptureName, Qt::CaseInsensitive) == 0);
	if (!generic || file.isEmpty())
		return;

	QString dir = current.isEmpty() ? documentsDir() : QFileInfo(current).path();
	if (dir.isEmpty() || dir == QLatin1String("."))
		dir = documentsDir();

	const QString updated = QDir(dir).filePath(file);
	autoCaptureName = file;
	if (updated != capturePath->text())
		capturePath->setText(updated);
}

void MainWindow::onAnalyze()
{
	if (job->isRunning())
		return;
	applyCaptureName();
	saveSettings();
	if (recordingPath->text().trimmed().isEmpty())
	{
		statusLabel->setText("Elegí la grabación.");
		return;
	}
	if (capturePath->text().trimmed().isEmpty())
	{
		statusLabel->setText("Elegí dónde guardar la captura.");
		return;
	}

	job->kind = CaptureJob::Kind::Analyze;
	job->duration = static_cast<float>(durationBox->value());
	job->repeats = repeatsBox->value();
	job->sendDb = static_cast<float>(sendBox->value());
	job->recordingFile = recordingPath->text().trimmed();
	job->captureFile = capturePath->text().trimmed();
	job->name = nameEdit->text().trimmed();
	job->device = deviceEdit->text().trimmed();
	job->author = authorEdit->text().trimmed();
	job->notes = notesEdit->text().trimmed();
	setBusy(true);
	job->start();
}

void MainWindow::onJobFinished()
{
	setBusy(false);
	report->setPlainText(job->summary);
	statusLabel->setText(job->ok ? "Listo." : "No se pudo completar.");
	if (job->kind == CaptureJob::Kind::Analyze)
		plot->setResponse(job->bandHz, job->passBandsDb, job->irBandsDb);
}
