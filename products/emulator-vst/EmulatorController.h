#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <mutex>
#include <string>

namespace DevicesForge
{
	namespace Emulator
	{

		/**
		 * EmulatorController
		 *
		 * Parametros, editor VSTGUI (editor.uidesc) y las vistas propias del
		 * panel: los dos VU con aguja, el nombre y el autor de la captura.
		 */
		class EmulatorController : public Steinberg::Vst::EditController, public VSTGUI::VST3EditorDelegate
		{
		public:
			static Steinberg::FUnknown* createInstance(void*)
			{
				return static_cast<Steinberg::Vst::IEditController*>(new EmulatorController());
			}

			Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
															 Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
																Steinberg::Vst::ParamValue valueNormalized,
																Steinberg::Vst::String128 string) SMTG_OVERRIDE;
			Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
			Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

			VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
											const VSTGUI::IUIDescription* description,
											VSTGUI::VST3Editor* editor) override;

			/** Nombre de la captura cargada (vacio si no hay). */
			std::string getCaptureName() const;
			/** Autor de la captura (vacio si el archivo no lo trae). */
			std::string getCaptureAuthor() const;

		private:
			void chooseCapture();
			void setCaptureInfo(const std::string& name, const std::string& author);

			bool fileDialogBusy { false };
			double fileDialogIgnoreUntil { 0.0 };
			mutable std::mutex nameMutex;
			std::string captureName;
			std::string captureAuthor;
		};

	} // namespace Emulator
} // namespace DevicesForge
