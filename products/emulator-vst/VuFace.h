#pragma once

#include <cmath>
#include <cstddef>

/* Geometria del cuadrante img/VU_Meter.png, en puntos de la imagen 1x
   (la 2x mide el doble). Medida sobre el arte: si se cambia la imagen,
   hay que volver a medir pivote, marcas, LED y tapa. */

namespace DevicesForge
{
	namespace Emulator
	{

		/** Tamano del cuadrante (1x). La imagen 2x es 698 x 392. */
		constexpr float VU_FACE_WIDTH = 349.0f;
		constexpr float VU_FACE_HEIGHT = 196.0f;

		/** Pivote de la aguja: cae debajo de la tapa, fuera de la ventana. */
		constexpr float VU_PIVOT_X = 170.55f;
		constexpr float VU_PIVOT_Y = 204.75f;
		/** Largo de la aguja desde el pivote: llega a las marcas sin tapar los numeros. */
		constexpr float VU_NEEDLE_LENGTH = 139.0f;

		/** Ventana del cuadrante: la aguja no se dibuja fuera de aca. */
		constexpr float VU_WINDOW_LEFT = 20.0f;
		constexpr float VU_WINDOW_TOP = 20.0f;
		constexpr float VU_WINDOW_RIGHT = 327.5f;
		constexpr float VU_WINDOW_BOTTOM = 170.5f;

		/** Recorte de la tapa del pivote (vu_cap.png), va encima de la aguja. */
		constexpr float VU_CAP_X = 141.0f;
		constexpr float VU_CAP_Y = 150.0f;

		/** LED de PEAK. */
		constexpr float VU_LED_X = 300.4f;
		constexpr float VU_LED_Y = 87.05f;
		constexpr float VU_LED_RADIUS = 4.4f;

		/** Angulos (grados, 0 = vertical, positivo a la derecha). */
		constexpr float VU_REST_DEG = -39.73f;
		constexpr float VU_PEG_DEG = 40.23f;

		struct VuMark
		{
			float db;
			float deg;
		};

		/** Las 13 marcas del arte, medidas desde el pivote. No siguen la ley
		    fisica (deflexion proporcional a la amplitud): se interpola. */
		constexpr VuMark VU_MARKS[] = {
			{-20.0f, -35.80f}, {-10.0f, -24.69f}, {-7.0f, -14.66f}, {-6.0f, -10.44f},
			{-5.0f, -5.36f},   {-4.0f, -0.65f},   {-3.0f, 4.91f},   {-2.0f, 9.73f},
			{-1.0f, 14.76f},   {0.0f, 19.61f},    {1.0f, 23.76f},   {2.0f, 28.16f},
			{3.0f, 32.38f}};
		constexpr size_t VU_NUM_MARKS = sizeof(VU_MARKS) / sizeof(VU_MARKS[0]);

		/*
		 * vuNeedleAngleDeg
		 *
		 * Pseudocodigo:
		 * 1. Sin senal, la aguja queda en reposo
		 * 2. Debajo de -20, ir lineal en amplitud hasta el reposo
		 * 3. Entre marcas, interpolar en dB
		 * 4. Arriba de +3, seguir la ultima pendiente hasta el tope
		 */
		inline float vuNeedleAngleDeg(float needle)
		{
			/* Sin senal: reposo */
			if (!(needle > 0.0f))
				return VU_REST_DEG;

			/* Debajo de la primera marca: lineal en amplitud hasta el reposo */
			const float firstAmp = std::pow(10.0f, VU_MARKS[0].db / 20.0f);
			if (needle <= firstAmp)
				return VU_REST_DEG + (VU_MARKS[0].deg - VU_REST_DEG) * (needle / firstAmp);

			/* Entre marcas: interpolo en dB */
			const float db = 20.0f * std::log10(needle);
			for (size_t i = 1; i < VU_NUM_MARKS; ++i)
			{
				if (db <= VU_MARKS[i].db)
				{
					const VuMark& a = VU_MARKS[i - 1];
					const VuMark& b = VU_MARKS[i];
					return a.deg + (b.deg - a.deg) * (db - a.db) / (b.db - a.db);
				}
			}

			/* Pasado +3: sigo la ultima pendiente y freno en el tope */
			const VuMark& a = VU_MARKS[VU_NUM_MARKS - 2];
			const VuMark& b = VU_MARKS[VU_NUM_MARKS - 1];
			const float deg = b.deg + (b.deg - a.deg) * (db - b.db) / (b.db - a.db);
			return deg < VU_PEG_DEG ? deg : VU_PEG_DEG;
		}

	} // namespace Emulator
} // namespace DevicesForge
