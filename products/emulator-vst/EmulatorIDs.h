#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cmath>

namespace DevicesForge
{
	namespace Emulator
	{

		static const Steinberg::FUID ProcessorUID(0x5D3A71C2, 0x8E4B4F09, 0xA61D27B3, 0xC09E54F1);
		static const Steinberg::FUID ControllerUID(0x2B8F06DE, 0x47C14A5D, 0x9F3E81A7, 0x6C25D90B);

		/** Parametros del Emulador. Los IDs quedan fijos: los usa la automatizacion
		    guardada en los proyectos. */
		enum ParamIDs : Steinberg::Vst::ParamID
		{
			INPUT_GAIN = 1,
			MIX = 2,
			OUTPUT_GAIN = 3,
			BYPASS = 4,
			/** Boton: abre el dialogo de captura. No se automatiza. */
			LOAD_CAPTURE = 5,

			/** Salidas del procesador hacia el editor (solo lectura). */
			METER_IN = 100,
			METER_OUT = 101,
			PEAK_IN = 102,
			PEAK_OUT = 103
		};

		/** Rango de las ganancias de entrada y salida. */
		constexpr float GAIN_MIN_DB = -12.0f;
		constexpr float GAIN_MAX_DB = 12.0f;

		/** Nivel eficaz que marca 0 VU (alineacion habitual en digital). */
		constexpr float VU_REFERENCE_DBFS = -18.0f;
		/** El LED de PEAK prende desde este pico de muestra. */
		constexpr float PEAK_LED_DBFS = -3.0f;
		/** Tiempo minimo que el LED queda prendido (ms). */
		constexpr int PEAK_LED_HOLD_MS = 300;

		/** Version del estado guardado en el proyecto. */
		constexpr Steinberg::int32 STATE_VERSION = 1;

		/** Ganancia lineal desde el valor normalizado (0.5 = 0 dB). */
		inline float gainFromNormalized(double normalized)
		{
			const double db = GAIN_MIN_DB + normalized * (GAIN_MAX_DB - GAIN_MIN_DB);
			return static_cast<float>(std::pow(10.0, db / 20.0));
		}

	} // namespace Emulator
} // namespace DevicesForge
