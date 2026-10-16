#pragma once

#include <cstdint>

#include "DFConstants.h"

/* Constantes de medicion: senales de excitacion, ventana de captura,
   deconvolucion y post-proceso de la IR. */

namespace DevicesForge
{

	enum class SignalType : int32_t
	{
		SineSweep = 0,
		Dirac = 1,
		PinkNoise = 2,
		MLS = 3
	};

	constexpr int32_t NUM_SIGNAL_TYPES = 4;
	constexpr float SIGNAL_DURATION_MIN = 0.5f;
	constexpr float SIGNAL_DURATION_MAX = 5.0f;
	constexpr float SIGNAL_DURATION_DEFAULT = 1.0f;
	constexpr float SWEEP_FREQ_START_HZ = 20.0f;
	constexpr float SWEEP_FREQ_END_HZ = 20000.0f;

	constexpr int32_t MLS_REGISTER_BITS = 16;
	constexpr int32_t MLS_PERIOD = (1 << MLS_REGISTER_BITS) - 1;

	constexpr float CAPTURE_PRE_SEC = 0.100f;
	constexpr float CAPTURE_POST_TAIL_SEC = 0.250f;
	constexpr float CAPTURE_MONITOR_SEC =
		CAPTURE_PRE_SEC + SIGNAL_DURATION_MAX + CAPTURE_POST_TAIL_SEC + 0.5f;

	/** Deconvolución: regularización de Tikhonov relativa al pico de |Ref|².
	    Fuera de la banda del sweep el espectro de referencia es ~0; sin esto,
	    dividir por él amplifica ruido ultrasónico hasta dominar toda la IR. */
	constexpr float DECONV_REGULARIZATION = 1.0e-3f;

	constexpr float IR_NORMALIZE_PEAK_TARGET = 1.0f;
	constexpr float IR_KAISER_BETA_DEFAULT = 5.0f;
	constexpr float IR_TAIL_FADE_FRACTION = 0.25f;
	constexpr float CAPTURE_MIN_PEAK = 1.0e-4f;

	/** Umbrales de calidad para el veredicto en capture_log.txt. */
	constexpr float CAPTURE_CLIP_THRESHOLD = 0.99f;
	constexpr float CAPTURE_GOOD_PEAK_MIN = 0.05f;   // ~ -26 dBFS
	constexpr float CAPTURE_GOOD_SNR_DB = 20.0f;

} // DevicesForge
