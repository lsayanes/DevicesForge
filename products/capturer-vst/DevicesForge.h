#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "DFConstants.h"
#include "DFMeasureConstants.h"

/* Constantes propias del plugin de captura: identidad, IDs de parametros
   y mapeo valor normalizado <-> valor real. */

namespace DevicesForge 
{

	constexpr const char* PLUGIN_NAME = "DevicesForge";
	constexpr const char* PLUGIN_VENDOR = "lsayanes & DevicesForge";
	constexpr const int32_t PLUGIN_VERSION_MAJOR = 1;
	constexpr const int32_t PLUGIN_VERSION_MINOR = 0;
	constexpr const int32_t PLUGIN_VERSION_PATCH = 0;

	constexpr int32_t NUM_CAPTURE_LEVELS = 4;
	constexpr float CAPTURE_LEVELS[NUM_CAPTURE_LEVELS] = {
		-24.0f,  // Quiet
		-12.0f,  // Low
		0.0f,   // Nominal
		+6.0f    // Hot
	};

	/** Tono de calibración: continuo, sin captura. */
	constexpr float CALIBRATE_TONE_HZ = 1000.0f;

	enum class CalibrateSignal : int32_t
	{
		Tone1k = 0,
		Sweep = 1
	};

	constexpr int32_t NUM_CALIBRATE_SIGNALS = 2;

	/** Medidor InPeak: decaimiento del peak-hold (por bloque). */
	constexpr float METER_DECAY_PER_BLOCK = 0.85f;

	inline float signalDurationNormalizedDefault()
	{
		return (SIGNAL_DURATION_DEFAULT - SIGNAL_DURATION_MIN) /
			   (SIGNAL_DURATION_MAX - SIGNAL_DURATION_MIN);
	}

	inline float normalizedToDuration(float normalized)
	{
		if (normalized < 0.0f) normalized = 0.0f;
		if (normalized > 1.0f) normalized = 1.0f;
		return SIGNAL_DURATION_MIN + normalized * (SIGNAL_DURATION_MAX - SIGNAL_DURATION_MIN);
	}

	inline SignalType normalizedToSignalType(float normalized)
	{
		int32_t index = static_cast<int32_t>(normalized * static_cast<float>(NUM_SIGNAL_TYPES - 1) + 0.5f);
		if (index < 0) index = 0;
		if (index >= NUM_SIGNAL_TYPES) index = NUM_SIGNAL_TYPES - 1;
		return static_cast<SignalType>(index);
	}

	inline CalibrateSignal normalizedToCalibrateSignal(float normalized)
	{
		int32_t index = static_cast<int32_t>(normalized * static_cast<float>(NUM_CALIBRATE_SIGNALS - 1) + 0.5f);
		if (index < 0) index = 0;
		if (index >= NUM_CALIBRATE_SIGNALS) index = NUM_CALIBRATE_SIGNALS - 1;
		return static_cast<CalibrateSignal>(index);
	}

	namespace PluginParamIDs 
	{
		constexpr uint32_t IR_SELECT = 1001;
		constexpr uint32_t MIX = 1002;
		constexpr uint32_t OUTPUT_GAIN = 1003;
		/** 1004 retirado: no reutilizar (proyectos viejos guardaban un parametro ahi). */
		constexpr uint32_t SIGNAL_TYPE = 1005;
		constexpr uint32_t SIGNAL_DURATION = 1006;
		constexpr uint32_t GENERATE = 1007;
		constexpr uint32_t EXPORT_FORMAT = 1008;
		constexpr uint32_t EXPORT = 1009;
		constexpr uint32_t INPUT_PEAK = 1010;
		/** Si está On, al disparar Generate se borra exports/latest antes de escribir. */
		constexpr uint32_t CLEAR_LATEST = 1011;
		/** Solo lectura: texto de version en el panel del host (units = FULL_VERSION_STR). */
		constexpr uint32_t VERSION_LABEL = 1012;
		/** Solo lectura: largo en ms de la IR cargada (0 = sin IR → passthrough). */
		constexpr uint32_t IR_LENGTH_MS = 1013;
		/** On: envía tono/sweep de calibración (sin captura). Oculto en el panel. */
		constexpr uint32_t CALIBRATE = 1014;
		constexpr uint32_t CALIBRATE_SIGNAL = 1015;
		/** Nivel del sweep hacia el dispositivo (−12…+12 dB). No afecta la emulación. */
		constexpr uint32_t SEND = 1016;
		/** Off→On: diálogo para guardar la IR en un .wav float a elección. */
		constexpr uint32_t SAVE_IR = 1017;
		/** Off→On: diálogo para cargar una IR guardada. */
		constexpr uint32_t LOAD_IR = 1018;
	}

	/** Tope del indicador IRLen (ms). */
	constexpr float IR_LENGTH_METER_MAX_MS = 2000.0f;

	struct CaptureSession 
	{
		bool active = false;
		int32_t currentLevel = 0;
		int32_t totalLevels = NUM_CAPTURE_LEVELS;
		std::vector<std::vector<float>> capturedIRs;
	};

} // DevicesForge
