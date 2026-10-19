#pragma once

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"

#include "pluginterfaces/vst/vsttypes.h"

#include <chrono>

namespace DevicesForge
{
	namespace Emulator
	{

		class EmulatorController;

		/**
		 * VuMeterView
		 *
		 * VU con aguja sobre el cuadrante vu_face. La balistica ya viene del
		 * procesador (VuMeter): la vista solo convierte la posicion en angulo
		 * con la escala del arte (VuFace.h) y prende el LED de PEAK.
		 * El tamano de la vista tiene que ser el del cuadrante (349 x 196).
		 */
		class VuMeterView : public VSTGUI::CView
		{
		public:
			VuMeterView(const VSTGUI::CRect& size, EmulatorController* controller, Steinberg::Vst::ParamID needleId,
						Steinberg::Vst::ParamID peakId, VSTGUI::CBitmap* face, VSTGUI::CBitmap* cap);

			void draw(VSTGUI::CDrawContext* context) override;
			bool attached(VSTGUI::CView* parent) override;
			bool removed(VSTGUI::CView* parent) override;

		private:
			void poll();
			void drawNeedle(VSTGUI::CDrawContext* context, const VSTGUI::CRect& face);
			void drawLed(VSTGUI::CDrawContext* context, const VSTGUI::CRect& face);

			EmulatorController* controller;
			Steinberg::Vst::ParamID needleId;
			Steinberg::Vst::ParamID peakId;
			VSTGUI::SharedPointer<VSTGUI::CBitmap> face;
			VSTGUI::SharedPointer<VSTGUI::CBitmap> cap;
			VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;

			float angleDeg;
			bool ledOn { false };
			std::chrono::steady_clock::time_point ledUntil {};
		};

		/**
		 * CaptureNameView
		 *
		 * Etiqueta con el nombre o el autor de la captura. Fuente y colores
		 * vienen del uidesc (class="CTextLabel").
		 */
		class CaptureNameView : public VSTGUI::CTextLabel
		{
		public:
			enum class Field
			{
				Name,
				Author
			};

			CaptureNameView(const VSTGUI::CRect& size, EmulatorController* controller, Field field);

			bool attached(VSTGUI::CView* parent) override;
			bool removed(VSTGUI::CView* parent) override;

		private:
			void poll();

			EmulatorController* controller;
			Field field;
			VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
		};

		/**
		 * VersionView
		 *
		 * FULL_VERSION_STR en una esquina. No se actualiza: es la version compilada.
		 */
		class VersionView : public VSTGUI::CTextLabel
		{
		public:
			explicit VersionView(const VSTGUI::CRect& size);

			bool attached(VSTGUI::CView* parent) override;
		};

	} // namespace Emulator
} // namespace DevicesForge
