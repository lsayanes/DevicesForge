#pragma once

#include <cstdint>
#include <vector>

namespace DevicesForge
{

	/**
	 * IRResampler
	 *
	 * Remuestrea una IR con sinc enventanado (Hann, 32 taps).
	 * Si las tasas coinciden, copia. Conserva la ganancia de continua.
	 */
	class IRResampler
	{
	public:
		/*
		 * resample
		 *
		 * Pseudocodigo:
		 * 1. Si las tasas coinciden, copiar
		 * 2. Calcular el largo de salida
		 * 3. Para cada muestra, sumar el sinc alrededor de la posicion fuente
		 * 4. Normalizar los pesos para no cambiar el nivel
		 */
		static bool resample(const float* input,
							 int32_t inputLength,
							 double inputRate,
							 std::vector<float>& output,
							 double outputRate);
	};

} // namespace DevicesForge
