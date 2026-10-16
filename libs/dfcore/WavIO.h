#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DevicesForge
{

	/** Audio PCM de un WAV. channels[c][n] es la muestra n del canal c. */
	struct WavBuffer
	{
		double sampleRate = 0.0;
		std::vector<std::vector<float>> channels;
	};

	/**
	 * WavIO
	 *
	 * Lee y escribe WAV poco-endian: PCM 16, PCM 24 y float 32.
	 * Acepta WAVEFORMATEXTENSIBLE si el subformato es uno de esos tres.
	 */
	class WavIO
	{
	public:
		/*
		 * load
		 *
		 * Pseudocodigo:
		 * 1. Verificar cabecera RIFF/WAVE
		 * 2. Leer fmt y data
		 * 3. Convertir cada canal a float [-1, 1]
		 */
		static bool load(const std::string& path, WavBuffer& out);

		/*
		 * save
		 *
		 * Pseudocodigo:
		 * 1. Armar cabecera segun bits (16, 24 o 32 float)
		 * 2. Intercalar los canales y escribir
		 */
		static bool save(const std::string& path, const WavBuffer& in, int32_t bitsPerSample);
	};

} // namespace DevicesForge
