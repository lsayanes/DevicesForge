#pragma once

#include <cmath>
#include <cstdint>
#include <string>

/* Constantes de audio comunes a cualquier producto (sin VST ni medicion). */

namespace DevicesForge
{

	/** Pi and common multiples (DSP, tests, host). */
	constexpr double kPi = 3.14159265358979323846;
	constexpr float kPiF = static_cast<float>(kPi);
	constexpr double kTwoPi = 2.0 * kPi;
	constexpr float kTwoPiF = static_cast<float>(kTwoPi);

	constexpr double SAMPLE_RATE_DEFAULT = 48000.0;
	constexpr int32_t NUM_CHANNELS = 2;
	constexpr int32_t MAX_IR_LENGTH = 65536;
	constexpr int32_t FFT_SIZE = 4096;

	/** Convolución en tiempo real (DynamicConvolver): FFT y bloque interno.
	    blockSize = CONV_FFT_SIZE / 2 → latencia del path convolucionado (~21 ms @48k). */
	constexpr int32_t CONV_FFT_SIZE = 2048;
	/** El seco del Mix se retrasa hasta el pico de la IR, como maximo esto (~5 ms @48k).
	    Si el pico esta mas lejos, el seco queda en 0. */
	constexpr int32_t CONV_DRY_ALIGN_MAX = 256;

	/** Tope de seguridad al restaurar una IR desde el estado del proyecto:
	    evita reservar memoria absurda si el stream viene corrupto. */
	constexpr int32_t IR_STATE_MAX_SAMPLES = 48000 * 10;

	/** Medidores: piso de la escala en dBFS. */
	constexpr float METER_FLOOR_DB = -60.0f;

	/** Convierte amplitud lineal a dBFS con piso en METER_FLOOR_DB. */
	inline float linearToDbfs(float linear)
	{
		if (linear <= 0.0f)
			return METER_FLOOR_DB;
		const float db = 20.0f * std::log10(linear);
		return (db < METER_FLOOR_DB) ? METER_FLOOR_DB : db;
	}

	struct IRMetadata
	{
		std::string name;
		std::string console;
		std::string channel;
		float levelDB = 0.0f;
		int32_t sampleRate = 48000;
		int32_t length = 0;
	};

} // DevicesForge
