#pragma once

#include "CaptureFile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace DevicesForge
{

	/** Parametros del barrido. analyze usa duration y repeats; el rate lo toma del WAV. */
	struct SweepSpec
	{
		double sampleRate = 48000.0;
		float durationSeconds = 5.0f;
		int32_t repeats = 3;
	};

	/**
	 * Posiciones del archivo de barrido, en muestras.
	 * silencio + marca + (barrido + cola) * N + marca + silencio.
	 * La cola (0,5 s) es el largo maximo de IR entre repeticiones.
	 */
	struct SweepLayout
	{
		double sampleRate = 0.0;
		int32_t repeats = 0;
		int32_t leadSilence = 0;
		int32_t syncLength = 0;
		int32_t gapLength = 0;
		int32_t sweepLength = 0;
		int32_t tailSilence = 0;
		/** Muestras desde el inicio de la primera marca hasta el inicio de la ultima. */
		int32_t syncSpan = 0;
		int32_t totalLength = 0;
	};

	struct AnalyzeOptions
	{
		SweepSpec sweep;
		std::string name;
		std::string device;
		std::string author;
		std::string notes;
		float sendDb = 0.0f;
	};

	struct QualityReport
	{
		bool ok = false;
		double speedRatio = 1.0;
		int32_t repeatsUsed = 0;
		/** El equipo invierte la polaridad. La IR de fase minima sale positiva igual. */
		bool polarityInverted = false;
		/** Muestra del pico. El plugin retrasa el seco hasta ahi. */
		int32_t peakIndex = 0;
		std::string text;
	};

	/**
	 * OfflineCapturer
	 *
	 * Arma el barrido y recupera una captura desde la grabacion.
	 * Corrige la velocidad media entre las dos marcas. No corrige wow ni flutter.
	 */
	class OfflineCapturer
	{
	public:
		/*
		 * makeSweep
		 *
		 * Pseudocodigo:
		 * 1. Armar el barrido y la marca de sync
		 * 2. Escribir silencio, marca, N barridos con cola, marca y silencio
		 */
		static bool makeSweep(const SweepSpec& spec, std::vector<float>& samples, SweepLayout& layout);

		/*
		 * analyze
		 *
		 * Pseudocodigo:
		 * 1. Encontrar las dos marcas de sync
		 * 2. Si la distancia no es la esperada, remuestrear la grabacion
		 * 3. Deconvolucionar cada barrido y promediar |H|^2 (sin sumar IRs)
		 * 4. Armar la IR de fase minima: pico al inicio y positivo (Mix en fase)
		 * 5. Ventanear, normalizar en banda y armar el reporte
		 */
		static bool analyze(const float* recording,
							int32_t recordingLength,
							double sampleRate,
							const AnalyzeOptions& options,
							CaptureData& capture,
							QualityReport& report);
	};

} // namespace DevicesForge
