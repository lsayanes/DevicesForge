#pragma once

#include <QWidget>

#include <vector>

/**
 * ResponsePlot
 *
 * Respuesta en frecuencia de la toma: una curva por repeticion y la de la IR guardada.
 * Eje X logaritmico de 20 Hz a 20 kHz, eje Y en dB relativos a 1 kHz.
 */
class ResponsePlot : public QWidget
{
public:
	explicit ResponsePlot(QWidget* parent = nullptr);

	void setResponse(const std::vector<float>& hz,
					 const std::vector<std::vector<float>>& passes,
					 const std::vector<float>& ir);
	void clear();

	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	QRectF plotArea() const;
	double xFor(double hz, const QRectF& area) const;
	double yFor(double db, const QRectF& area) const;
	void fitRange();

	std::vector<float> bandHz;
	std::vector<std::vector<float>> passDb;
	std::vector<float> irDb;
	float dbMin { -12.0f };
	float dbMax { 6.0f };
	int hoverBand { -1 };
};
