#include "EmulatorViews.h"
#include "EmulatorController.h"
#include "EmulatorIDs.h"
#include "VuFace.h"
#include "VuMeter.h"
#include "version.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

#include <cmath>

using namespace VSTGUI;

namespace DevicesForge
{
	namespace Emulator
	{

		namespace
		{
			constexpr uint32_t UI_POLL_MS = 33;
			constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
			/** Cambios de angulo menores no se redibujan. */
			constexpr float ANGLE_EPSILON_DEG = 0.02f;

			/** Ancho de la aguja: mas gruesa en el pivote que en la punta. */
			constexpr CCoord NEEDLE_BASE_HALF = 1.5;
			constexpr CCoord NEEDLE_TIP_HALF = 0.55;

			const CColor NEEDLE_COLOR(14, 12, 11, 250);
			const CColor NEEDLE_SHADOW(0, 0, 0, 55);
			const CPoint NEEDLE_SHADOW_OFFSET(1.0, 1.6);

			/*
			 * fillNeedle
			 *
			 * Pseudocodigo:
			 * 1. Armar el trapecio desde el pivote hasta la punta
			 * 2. Rellenarlo con el color pedido
			 */
			void fillNeedle(CDrawContext* context, const CPoint& pivot, double angleDeg, const CColor& color)
			{
				const double s = std::sin(angleDeg * DEG_TO_RAD);
				const double c = std::cos(angleDeg * DEG_TO_RAD);
				const CPoint tip(pivot.x + VU_NEEDLE_LENGTH * s, pivot.y - VU_NEEDLE_LENGTH * c);

				/* Armo el trapecio: perpendicular a la aguja = (c, s) */
				SharedPointer<CGraphicsPath> path = owned(context->createGraphicsPath());
				if (!path)
					return;
				path->beginSubpath(CPoint(pivot.x - NEEDLE_BASE_HALF * c, pivot.y - NEEDLE_BASE_HALF * s));
				path->addLine(CPoint(tip.x - NEEDLE_TIP_HALF * c, tip.y - NEEDLE_TIP_HALF * s));
				path->addLine(CPoint(tip.x + NEEDLE_TIP_HALF * c, tip.y + NEEDLE_TIP_HALF * s));
				path->addLine(CPoint(pivot.x + NEEDLE_BASE_HALF * c, pivot.y + NEEDLE_BASE_HALF * s));
				path->closeSubpath();

				/* Lo relleno */
				context->setFillColor(color);
				context->drawGraphicsPath(path, CDrawContext::kPathFilled);
			}

			/* Circulo con degrade radial (para el LED). */
			void fillGlow(CDrawContext* context, const CPoint& center, CCoord radius, const CColor& inner,
						  const CColor& outer)
			{
				SharedPointer<CGraphicsPath> path = owned(context->createGraphicsPath());
				SharedPointer<CGradient> gradient = owned(CGradient::create(0.0, 1.0, inner, outer));
				if (!path || !gradient)
					return;
				path->addEllipse(CRect(center.x - radius, center.y - radius, center.x + radius, center.y + radius));
				context->fillRadialGradient(path, *gradient, center, radius);
			}
		}

		VuMeterView::VuMeterView(const CRect& size, EmulatorController* owner, Steinberg::Vst::ParamID needle,
								 Steinberg::Vst::ParamID peak, CBitmap* faceBitmap, CBitmap* capBitmap)
			: CView(size), controller(owner), needleId(needle), peakId(peak), face(faceBitmap), cap(capBitmap),
			  angleDeg(VU_REST_DEG)
		{
			setTransparency(true);
		}

		bool VuMeterView::attached(CView* parent)
		{
			if (!CView::attached(parent))
				return false;
			timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { poll(); }, UI_POLL_MS, true);
			return true;
		}

		bool VuMeterView::removed(CView* parent)
		{
			if (timer)
			{
				timer->stop();
				timer = nullptr;
			}
			return CView::removed(parent);
		}

		/*
		 * poll
		 *
		 * Pseudocodigo:
		 * 1. Leer la aguja y el pico que mando el procesador
		 * 2. Convertir la aguja en angulo con la escala del cuadrante
		 * 3. Prender el LED con el pico y retenerlo PEAK_LED_HOLD_MS
		 * 4. Redibujar solo si algo cambio
		 */
		void VuMeterView::poll()
		{
			if (!controller)
				return;

			/* Leo la aguja y la paso a angulo */
			const float needle = static_cast<float>(controller->getParamNormalized(needleId)) * VU_NEEDLE_MAX;
			const float angle = vuNeedleAngleDeg(needle);

			/* Retengo el LED: un pico de una muestra tiene que verse */
			static const float ledThreshold = std::pow(10.0f, PEAK_LED_DBFS / 20.0f);
			const auto now = std::chrono::steady_clock::now();
			if (controller->getParamNormalized(peakId) >= ledThreshold)
				ledUntil = now + std::chrono::milliseconds(PEAK_LED_HOLD_MS);
			const bool lit = now < ledUntil;

			/* Redibujo solo si cambio */
			if (std::abs(angle - angleDeg) > ANGLE_EPSILON_DEG || lit != ledOn)
			{
				angleDeg = angle;
				ledOn = lit;
				invalid();
			}
		}

		void VuMeterView::drawNeedle(CDrawContext* context, const CRect& faceRect)
		{
			/* Recorto a la ventana: la aguja no pisa el marco */
			const CRect window(faceRect.left + VU_WINDOW_LEFT, faceRect.top + VU_WINDOW_TOP,
							   faceRect.left + VU_WINDOW_RIGHT, faceRect.top + VU_WINDOW_BOTTOM);
			ConcatClip clip(*context, window);
			if (clip.isEmpty())
				return;

			/* Dibujo la sombra y despues la aguja */
			const CPoint pivot(faceRect.left + VU_PIVOT_X, faceRect.top + VU_PIVOT_Y);
			fillNeedle(context, CPoint(pivot.x + NEEDLE_SHADOW_OFFSET.x, pivot.y + NEEDLE_SHADOW_OFFSET.y), angleDeg,
					   NEEDLE_SHADOW);
			fillNeedle(context, pivot, angleDeg, NEEDLE_COLOR);
		}

		void VuMeterView::drawLed(CDrawContext* context, const CRect& faceRect)
		{
			const CPoint center(faceRect.left + VU_LED_X, faceRect.top + VU_LED_Y);
			/* Halo y despues el LED encendido */
			fillGlow(context, center, VU_LED_RADIUS * 2.6, CColor(255, 70, 40, 150), CColor(255, 70, 40, 0));
			fillGlow(context, center, VU_LED_RADIUS, CColor(255, 214, 180, 255), CColor(226, 34, 22, 255));
		}

		/*
		 * draw
		 *
		 * Pseudocodigo:
		 * 1. Dibujar el cuadrante
		 * 2. Dibujar la aguja recortada a la ventana
		 * 3. Tapar el pivote con la tapa del arte
		 * 4. Si hubo pico, dibujar el LED encendido
		 */
		void VuMeterView::draw(CDrawContext* context)
		{
			const CRect r = getViewSize();
			context->setDrawMode(kAntiAliasing | kNonIntegralMode);

			/* Dibujo el cuadrante (sin imagen, un fondo neutro) */
			if (face)
				face->draw(context, r);
			else
			{
				context->setFillColor(CColor(40, 36, 30, 255));
				context->drawRect(r, kDrawFilled);
			}

			/* Dibujo la aguja y la tapa del pivote encima */
			drawNeedle(context, r);
			if (cap)
			{
				const CRect capRect(CPoint(r.left + VU_CAP_X, r.top + VU_CAP_Y), cap->getSize());
				cap->draw(context, capRect);
			}

			/* Dibujo el LED si hubo pico */
			if (ledOn)
				drawLed(context, r);

			setDirty(false);
		}

		CaptureNameView::CaptureNameView(const CRect& size, EmulatorController* owner, Field which)
			: CTextLabel(size), controller(owner), field(which)
		{
		}

		bool CaptureNameView::attached(CView* parent)
		{
			if (!CTextLabel::attached(parent))
				return false;
			poll();
			timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { poll(); }, 250, true);
			return true;
		}

		bool CaptureNameView::removed(CView* parent)
		{
			if (timer)
			{
				timer->stop();
				timer = nullptr;
			}
			return CTextLabel::removed(parent);
		}

		void CaptureNameView::poll()
		{
			if (!controller)
				return;
			/* El nombre vacio avisa que no hay captura; el autor vacio no muestra nada */
			std::string value = field == Field::Author ? controller->getCaptureAuthor() : controller->getCaptureName();
			if (field == Field::Name && value.empty())
				value = "Sin captura: cargá un .dfcapture o un WAV";
			const UTF8String text(value);
			if (getText() != text)
				setText(text);
		}

		VersionView::VersionView(const CRect& size) : CTextLabel(size) {}

		bool VersionView::attached(CView* parent)
		{
			if (!CTextLabel::attached(parent))
				return false;
			setText(FULL_VERSION_STR);
			return true;
		}

	} // namespace Emulator
} // namespace DevicesForge
