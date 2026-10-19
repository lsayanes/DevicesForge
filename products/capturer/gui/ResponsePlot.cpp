#include "ResponsePlot.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace
{

	constexpr double kMinHz = 20.0;
	constexpr double kMaxHz = 20000.0;
	constexpr int kLeft = 44;
	constexpr int kRight = 10;
	constexpr int kTop = 26;
	constexpr int kBottom = 22;

	const QColor kBackground("#121416");
	const QColor kBorder("#4a5058");
	const QColor kGrid("#2c3138");
	const QColor kZero("#6a7078");
	const QColor kText("#c4c8d0");
	const QColor kIr("#7dccc4");
	const QColor kPasses[] = {QColor("#e69440"), QColor("#9a8cd8"), QColor("#d07090"), QColor("#c8b850"),
							  QColor("#6aa0e0"), QColor("#a0a8b0"), QColor("#e07050"), QColor("#80c080")};

	QColor passColor(size_t pass)
	{
		return kPasses[pass % (sizeof(kPasses) / sizeof(kPasses[0]))];
	}

	QString hzText(double hz)
	{
		if (hz < 1000.0)
			return QString::number(hz, 'f', 0) + " Hz";
		QString text = QString::number(hz / 1000.0, 'f', hz < 10000.0 ? 1 : 0);
		if (text.endsWith(".0"))
			text.chop(2);
		return text.replace('.', ',') + " kHz";
	}

	QString dbText(float db)
	{
		const QString text = QString::number(db, 'f', 1).replace('.', ',');
		return db > 0.05f ? "+" + text : text;
	}

} // namespace

ResponsePlot::ResponsePlot(QWidget* parent)
	: QWidget(parent)
{
	setMouseTracking(true);
}

/*
 * setResponse
 *
 * Pseudocodigo:
 * 1. Copiar las bandas, las repeticiones y la IR (descartar curvas de otro largo)
 * 2. Ajustar el rango de dB y redibujar
 */
void ResponsePlot::setResponse(const std::vector<float>& hz,
							   const std::vector<std::vector<float>>& passes,
							   const std::vector<float>& ir)
{
	/* Copio solo las curvas que coinciden con las bandas */
	bandHz = hz;
	passDb.clear();
	for (const std::vector<float>& pass : passes)
	{
		if (pass.size() == hz.size())
			passDb.push_back(pass);
	}
	irDb = (ir.size() == hz.size()) ? ir : std::vector<float> {};
	hoverBand = -1;

	/* Ajusto el rango y redibujo */
	fitRange();
	update();
}

void ResponsePlot::clear()
{
	setResponse({}, {}, {});
}

QSize ResponsePlot::minimumSizeHint() const
{
	return {360, 200};
}

QRectF ResponsePlot::plotArea() const
{
	return QRectF(kLeft, kTop, std::max(width() - kLeft - kRight, 10), std::max(height() - kTop - kBottom, 10));
}

double ResponsePlot::xFor(double hz, const QRectF& area) const
{
	const double t = std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
	return area.left() + t * area.width();
}

double ResponsePlot::yFor(double db, const QRectF& area) const
{
	const double t = (db - dbMin) / (dbMax - dbMin);
	return area.bottom() - t * area.height();
}

/*
 * fitRange
 *
 * Pseudocodigo:
 * 1. Buscar el minimo y el maximo de todas las curvas
 * 2. Redondear a multiplos de 3 dB con margen, entre -30 y +18 dB
 */
void ResponsePlot::fitRange()
{
	/* Busco los extremos de todas las curvas */
	float low = 0.0f;
	float high = 0.0f;
	for (const std::vector<float>& pass : passDb)
	{
		for (float db : pass)
		{
			low = std::min(low, db);
			high = std::max(high, db);
		}
	}
	for (float db : irDb)
	{
		low = std::min(low, db);
		high = std::max(high, db);
	}

	/* Redondeo a 3 dB con al menos +-6 dB visibles */
	dbMin = std::clamp(std::floor((low - 1.0f) / 3.0f) * 3.0f, -30.0f, -6.0f);
	dbMax = std::clamp(std::ceil((high + 1.0f) / 3.0f) * 3.0f, 6.0f, 18.0f);
}

/*
 * paintEvent
 *
 * Pseudocodigo:
 * 1. Fondo, grilla de frecuencias y de dB con sus etiquetas
 * 2. Si no hay datos, un texto de ayuda
 * 3. Curvas de cada pasada (finas) y de la IR (gruesa)
 * 4. Leyenda arriba y, si el mouse esta encima, la banda con sus valores
 */
void ResponsePlot::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QRectF area = plotArea();
	QFont small = font();
	small.setPointSizeF(std::max(small.pointSizeF() - 2.0, 8.0));
	painter.setFont(small);

	/* Dibujo el fondo y la grilla de frecuencias */
	painter.fillRect(rect(), kBackground);
	painter.setPen(kBorder);
	painter.drawRect(rect().adjusted(0, 0, -1, -1));
	static const double kGridHz[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
	for (double hz : kGridHz)
	{
		const double x = xFor(hz, area);
		painter.setPen(kGrid);
		painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
		painter.setPen(kText);
		const QString label = hzText(hz).remove(" Hz").replace(" kHz", "k");
		painter.drawText(QRectF(x - 24, area.bottom() + 3, 48, kBottom - 4), Qt::AlignHCenter | Qt::AlignTop, label);
	}

	/* Dibujo la grilla de dB (cada 3 dB, o 6 si el rango es grande) */
	const float step = (dbMax - dbMin) > 24.0f ? 6.0f : 3.0f;
	for (float db = dbMin; db <= dbMax + 0.01f; db += step)
	{
		const double y = yFor(db, area);
		painter.setPen(QPen(std::fabs(db) < 0.01f ? kZero : kGrid, 1.0));
		painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
		painter.setPen(kText);
		painter.drawText(QRectF(2, y - 8, kLeft - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
						 QString::number(static_cast<int>(db)));
	}
	painter.drawText(QRectF(2, 4, kLeft + 120, 16), Qt::AlignLeft | Qt::AlignVCenter, "dB rel. 1 kHz");

	/* Si no hay datos, dejo un texto de ayuda */
	if (bandHz.empty())
	{
		painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
						 "Analizá una toma para ver la respuesta de cada pasada y de la IR.");
		return;
	}

	/* Dibujo cada curva dentro del area */
	auto drawCurve = [&](const std::vector<float>& curve, const QColor& color, double widthPx) {
		QPainterPath path;
		for (size_t b = 0; b < curve.size(); ++b)
		{
			const QPointF point(xFor(bandHz[b], area), yFor(std::clamp(curve[b], dbMin, dbMax), area));
			if (b == 0)
				path.moveTo(point);
			else
				path.lineTo(point);
		}
		painter.setPen(QPen(color, widthPx));
		painter.drawPath(path);
	};
	painter.save();
	painter.setClipRect(area);
	for (size_t pass = 0; pass < passDb.size(); ++pass)
		drawCurve(passDb[pass], passColor(pass), 1.2);
	if (!irDb.empty())
		drawCurve(irDb, kIr, 2.4);
	painter.restore();

	/* Armo la leyenda arriba a la derecha; si no entra junto al titulo, abrevio */
	const QFontMetrics metrics(small);
	auto passName = [&](size_t pass, bool shortName) {
		return QString(shortName ? "Rep. %1" : "Repetición %1").arg(pass);
	};
	double legendWidth = 0.0;
	for (size_t pass = 1; pass <= passDb.size(); ++pass)
		legendWidth += metrics.horizontalAdvance(passName(pass, false)) + 22;
	const bool shortNames = legendWidth > area.width() - 140;
	double x = area.right();
	auto legendItem = [&](const QString& name, const QColor& color) {
		x -= metrics.horizontalAdvance(name) + 22;
		painter.fillRect(QRectF(x, 10, 12, 3), color);
		painter.setPen(kText);
		painter.drawText(QPointF(x + 16, 15), name);
	};
	if (!irDb.empty())
		legendItem("IR", kIr);
	for (size_t pass = passDb.size(); pass > 0; --pass)
		legendItem(passName(pass, shortNames), passColor(pass - 1));

	/* Si el mouse esta encima, marco la banda y muestro los valores */
	if (hoverBand < 0 || hoverBand >= static_cast<int>(bandHz.size()))
		return;
	const size_t band = static_cast<size_t>(hoverBand);
	const double hx = xFor(bandHz[band], area);
	painter.setPen(QPen(kZero, 1.0, Qt::DashLine));
	painter.drawLine(QPointF(hx, area.top()), QPointF(hx, area.bottom()));

	QStringList lines;
	std::vector<QColor> colors;
	lines << hzText(bandHz[band]);
	colors.push_back(kText);
	for (size_t pass = 0; pass < passDb.size(); ++pass)
	{
		lines << QString("Repetición %1  %2 dB").arg(pass + 1).arg(dbText(passDb[pass][band]));
		colors.push_back(passColor(pass));
	}
	if (!irDb.empty())
	{
		lines << QString("IR  %1 dB").arg(dbText(irDb[band]));
		colors.push_back(kIr);
	}

	int boxWidth = 0;
	for (const QString& line : lines)
		boxWidth = std::max(boxWidth, metrics.horizontalAdvance(line));
	boxWidth += 12;
	const int lineHeight = metrics.height();
	const int boxHeight = lineHeight * static_cast<int>(lines.size()) + 8;
	double boxX = hx + 8;
	if (boxX + boxWidth > area.right())
		boxX = hx - 8 - boxWidth;
	const QRectF box(boxX, area.top() + 4, boxWidth, boxHeight);
	painter.fillRect(box, QColor(18, 20, 22, 230));
	painter.setPen(kBorder);
	painter.drawRect(box);
	for (int i = 0; i < lines.size(); ++i)
	{
		painter.setPen(colors[static_cast<size_t>(i)]);
		painter.drawText(QRectF(box.left() + 6, box.top() + 4 + i * lineHeight, boxWidth - 6, lineHeight),
						 Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
	}
}

/*
 * mouseMoveEvent
 *
 * Pseudocodigo:
 * 1. Pasar la x del mouse a frecuencia
 * 2. Elegir la banda mas cercana en escala logaritmica
 */
void ResponsePlot::mouseMoveEvent(QMouseEvent* event)
{
	const QRectF area = plotArea();
	int band = -1;
	if (!bandHz.empty() && area.contains(event->position()))
	{
		/* Paso la x a frecuencia y busco la banda mas cercana */
		const double t = (event->position().x() - area.left()) / area.width();
		const double hz = kMinHz * std::pow(kMaxHz / kMinHz, t);
		double best = 1.0e9;
		for (size_t b = 0; b < bandHz.size(); ++b)
		{
			const double distance = std::fabs(std::log(bandHz[b] / hz));
			if (distance < best)
			{
				best = distance;
				band = static_cast<int>(b);
			}
		}
	}
	if (band != hoverBand)
	{
		hoverBand = band;
		update();
	}
}

void ResponsePlot::leaveEvent(QEvent*)
{
	if (hoverBand >= 0)
	{
		hoverBand = -1;
		update();
	}
}
