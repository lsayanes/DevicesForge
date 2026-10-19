#include "EmulatorController.h"
#include "EmulatorIDs.h"
#include "EmulatorState.h"
#include "EmulatorViews.h"
#include "VuFace.h"

#include "FileDialog.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/uidescription/uiattributes.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace DevicesForge
{
	namespace Emulator
	{

		namespace
		{
			/** Tras cerrar el dialogo, ignoro el boton este tiempo (el host repite el valor). */
			constexpr double FILE_DIALOG_IGNORE_SEC = 0.75;

			double nowSeconds()
			{
				return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
			}

			/*
			 * formatTenths
			 *
			 * Pseudocodigo:
			 * 1. Redondear a decimas con enteros (sin depender del locale)
			 * 2. Armar signo, entero, coma decimal y unidad
			 */
			std::string formatTenths(double value, bool withSign, const char* unit)
			{
				/* Redondeo a decimas */
				const long long tenths = std::llround(value * 10.0);
				const long long magnitude = tenths < 0 ? -tenths : tenths;

				/* Armo el texto: el 0 va sin signo */
				std::string text;
				if (tenths < 0)
					text += '-';
				else if (withSign && tenths > 0)
					text += '+';
				text += std::to_string(magnitude / 10) + "," + std::to_string(magnitude % 10) + " " + unit;
				return text;
			}

			/* Medidor de solo lectura, oculto: no aparece en la lista del host. */
			Parameter* meterParameter(const char16* title, ParamID id)
			{
				return new RangeParameter(title, id, nullptr, 0.0, 1.0, 0.0, 0,
										  ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden);
			}
		}

		/*
		 * initialize
		 *
		 * Pseudocodigo:
		 * 1. Ganancias en dB (-12..+12), mezcla en %, bypass
		 * 2. Boton para cargar la captura
		 * 3. Agujas y picos de los VU, ocultos y de solo lectura
		 * 4. Doble clic en una perilla: vuelve a su valor de reposo
		 */
		tresult PLUGIN_API EmulatorController::initialize(FUnknown* context)
		{
			const tresult result = EditController::initialize(context);
			if (result != kResultOk)
				return result;

			/* Doble clic solo en las perillas. El resto sigue con Control + clic. */
			VSTGUI::CControl::CheckDefaultValueEventFunc = [](VSTGUI::CControl* control,
															   VSTGUI::MouseDownEvent& event) {
				if (!event.buttonState.isLeft())
					return false;
				if (dynamic_cast<VSTGUI::CKnobBase*>(control) != nullptr)
					return event.clickCount == 2;
				return event.modifiers.is(VSTGUI::ModifierKey::Control);
			};

			/* Ganancias, mezcla y bypass */
			parameters.addParameter(new RangeParameter(STR16("Input"), INPUT_GAIN, nullptr, GAIN_MIN_DB,
													   GAIN_MAX_DB, 0.0, 0, ParameterInfo::kCanAutomate));
			/* Abre en 100 %. El doble clic (defaultNormalizedValue) vuelve a 50 %. */
			auto* mix = new RangeParameter(STR16("Mix"), MIX, nullptr, 0.0, 100.0, 100.0, 0,
										   ParameterInfo::kCanAutomate);
			mix->getInfo().defaultNormalizedValue = 0.5;
			parameters.addParameter(mix);
			parameters.addParameter(new RangeParameter(STR16("Output"), OUTPUT_GAIN, nullptr, GAIN_MIN_DB,
													   GAIN_MAX_DB, 0.0, 0, ParameterInfo::kCanAutomate));
			parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
									ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, BYPASS);

			/* Boton de carga: no se automatiza ni aparece en el host */
			parameters.addParameter(STR16("Load Capture"), nullptr, 1, 0.0, ParameterInfo::kIsHidden, LOAD_CAPTURE);

			/* Agujas y picos */
			parameters.addParameter(meterParameter(STR16("VU In"), METER_IN));
			parameters.addParameter(meterParameter(STR16("VU Out"), METER_OUT));
			parameters.addParameter(meterParameter(STR16("Peak In"), PEAK_IN));
			parameters.addParameter(meterParameter(STR16("Peak Out"), PEAK_OUT));
			return kResultOk;
		}

		tresult PLUGIN_API EmulatorController::setComponentState(IBStream* stream)
		{
			if (!stream)
				return kResultFalse;

			/* Leo el estado del procesador salteando la IR */
			IBStreamer streamer(stream, kLittleEndian);
			StoredState state;
			if (!readState(streamer, state, nullptr))
				return kResultFalse;

			EditController::setParamNormalized(INPUT_GAIN, state.inputGain);
			EditController::setParamNormalized(MIX, state.mix);
			EditController::setParamNormalized(OUTPUT_GAIN, state.outputGain);
			EditController::setParamNormalized(BYPASS, state.bypass);
			setCaptureInfo(state.captureName, state.captureAuthor);
			return kResultOk;
		}

		tresult PLUGIN_API EmulatorController::setParamNormalized(ParamID tag, ParamValue value)
		{
			const tresult result = EditController::setParamNormalized(tag, value);
			if (result == kResultOk && tag == LOAD_CAPTURE && value >= 0.5)
				chooseCapture();
			return result;
		}

		/*
		 * chooseCapture
		 *
		 * Pseudocodigo:
		 * 1. Ignorar si ya hay un dialogo abierto o recien se cerro
		 * 2. Pedir el archivo y mandarle la ruta al procesador
		 * 3. Soltar el boton
		 */
		void EmulatorController::chooseCapture()
		{
			/* Ignoro repeticiones: el host puede reenviar el valor del boton */
			if (fileDialogBusy || nowSeconds() < fileDialogIgnoreUntil)
				return;

			/* Pido el archivo y aviso al procesador */
			fileDialogBusy = true;
			std::string path;
			if (chooseOpenIRPath(path))
			{
				IPtr<IMessage> message = owned(allocateMessage());
				if (message)
				{
					message->setMessageID(MSG_LOAD_CAPTURE);
					setUtf8Attribute(message->getAttributes(), ATTR_PATH, path);
					sendMessage(message);
				}
			}
			fileDialogBusy = false;
			fileDialogIgnoreUntil = nowSeconds() + FILE_DIALOG_IGNORE_SEC;

			/* Suelto el boton */
			EditController::setParamNormalized(LOAD_CAPTURE, 0.0);
		}

		/*
		 * getParamStringByValue
		 *
		 * Pseudocodigo:
		 * 1. Ganancias: "+1,5 dB" (el 0 sin signo)
		 * 2. Mezcla: "100 %"
		 * 3. Lo demas, como lo arma el SDK
		 */
		tresult PLUGIN_API EmulatorController::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
																	 String128 string)
		{
			/* Ganancias en dB con una decima */
			std::string text;
			if (tag == INPUT_GAIN || tag == OUTPUT_GAIN)
				text = formatTenths(GAIN_MIN_DB + valueNormalized * (GAIN_MAX_DB - GAIN_MIN_DB), true, "dB");
			/* Mezcla en % entero */
			else if (tag == MIX)
				text = std::to_string(std::llround(valueNormalized * 100.0)) + " %";
			else
				return EditController::getParamStringByValue(tag, valueNormalized, string);

			UString(string, str16BufferSize(String128)).fromAscii(text.c_str());
			return kResultOk;
		}

		tresult PLUGIN_API EmulatorController::notify(IMessage* message)
		{
			if (!message || !FIDStringsEqual(message->getMessageID(), MSG_CAPTURE_NAME))
				return EditController::notify(message);

			std::string name;
			std::string author;
			if (getUtf8Attribute(message->getAttributes(), ATTR_NAME, name))
			{
				getUtf8Attribute(message->getAttributes(), ATTR_AUTHOR, author);
				setCaptureInfo(name, author);
			}
			return kResultOk;
		}

		std::string EmulatorController::getCaptureName() const
		{
			std::lock_guard<std::mutex> lock(nameMutex);
			return captureName;
		}

		std::string EmulatorController::getCaptureAuthor() const
		{
			std::lock_guard<std::mutex> lock(nameMutex);
			return captureAuthor;
		}

		void EmulatorController::setCaptureInfo(const std::string& name, const std::string& author)
		{
			std::lock_guard<std::mutex> lock(nameMutex);
			captureName = name;
			captureAuthor = author;
		}

		IPlugView* PLUGIN_API EmulatorController::createView(FIDString name)
		{
			if (FIDStringsEqual(name, ViewType::kEditor))
				return new VSTGUI::VST3Editor(this, "view", "editor.uidesc");
			return nullptr;
		}

		/*
		 * createCustomView
		 *
		 * Pseudocodigo:
		 * 1. VUMeterIn / VUMeterOut: VU con aguja sobre las imagenes del uidesc
		 * 2. CaptureName / CaptureAuthor: nombre y autor de la captura
		 * 3. PluginVersion: FULL_VERSION_STR
		 * 4. Lo demas lo arma VSTGUI
		 */
		VSTGUI::CView* EmulatorController::createCustomView(VSTGUI::UTF8StringPtr name,
															 const VSTGUI::UIAttributes& attributes,
															 const VSTGUI::IUIDescription* description,
															 VSTGUI::VST3Editor* editor)
		{
			(void)attributes;
			(void)editor;
			if (!name)
				return nullptr;

			/* Armo los VU; el tamano lo pone el uidesc */
			const VSTGUI::CRect faceRect(0, 0, VU_FACE_WIDTH, VU_FACE_HEIGHT);
			const bool isIn = std::strcmp(name, "VUMeterIn") == 0;
			if (isIn || std::strcmp(name, "VUMeterOut") == 0)
			{
				return new VuMeterView(faceRect, this, isIn ? METER_IN : METER_OUT, isIn ? PEAK_IN : PEAK_OUT,
									   description->getBitmap("vu_face"), description->getBitmap("vu_cap"));
			}

			/* Armo el nombre, el autor y la version */
			if (std::strcmp(name, "CaptureName") == 0)
				return new CaptureNameView(VSTGUI::CRect(0, 0, 100, 20), this, CaptureNameView::Field::Name);
			if (std::strcmp(name, "CaptureAuthor") == 0)
				return new CaptureNameView(VSTGUI::CRect(0, 0, 100, 20), this, CaptureNameView::Field::Author);
			if (std::strcmp(name, "PluginVersion") == 0)
				return new VersionView(VSTGUI::CRect(0, 0, 80, 14));

			return nullptr;
		}

	} // namespace Emulator
} // namespace DevicesForge
