#pragma once

#include <cstdint>

namespace DevicesForge
{

	/** Amortiguamiento del instrumento: 1,5 % de sobrepico (IEC 60268-17). */
	constexpr double VU_DAMPING = 0.8007;
	/** Frecuencia natural (rad/s): con VU_DAMPING llega al 99 % en 300 ms. */
	constexpr double VU_NATURAL_RAD_S = 13.126;
	/** El VU rectifica: con esto un seno se lee como su valor eficaz. */
	constexpr double VU_RECTIFIED_TO_RMS = 1.1107207345395915;
	/** Tope mecanico de la aguja, en unidades de 0 VU (~ +5 dB). */
	constexpr float VU_NEEDLE_MAX = 1.8f;
	/** Tiempo de caida del pico retenido (constante de tiempo, s). */
	constexpr float VU_PEAK_RELEASE_SEC = 0.3f;

	/**
	 * VuMeter
	 *
	 * Aguja de VU como el instrumento real: rectifica la senal y la pasa por
	 * un sistema de segundo orden (masa, resorte y freno). 1.0 = 0 VU.
	 * Corre a tasa de audio para que la balistica no dependa del bloque.
	 * Ademas guarda el pico de muestra con caida, para el LED.
	 */
	class VuMeter
	{
	public:
		/*
		 * prepare
		 *
		 * Pseudocodigo:
		 * 1. Guardar el paso de tiempo de una muestra
		 * 2. Calcular la ganancia: 0 VU = referenceDbfs eficaces
		 * 3. Dejar la aguja en reposo
		 */
		void prepare(double sampleRate, float referenceDbfs);
		void reset();

		/*
		 * process
		 *
		 * Pseudocodigo:
		 * 1. Por muestra, tomar el mayor |x| entre canales
		 * 2. Mover la aguja: aceleracion = w^2 (entrada - posicion) - 2 z w velocidad
		 * 3. Frenarla en los topes (0 y VU_NEEDLE_MAX)
		 * 4. Actualizar el pico retenido del bloque
		 */
		void process(const float* const* channels, int32_t numChannels, int32_t numSamples);

		/** Posicion de la aguja: 1.0 = 0 VU. */
		float getNeedle() const { return static_cast<float>(position); }
		/** Pico de muestra con caida (lineal, 1.0 = 0 dBFS). */
		float getPeak() const { return peak; }

	private:
		double dt { 1.0 / 48000.0 };
		double sampleRate { 48000.0 };
		double drive { 1.0 };
		double position { 0.0 };
		double velocity { 0.0 };
		float peak { 0.0f };
	};

} // namespace DevicesForge
