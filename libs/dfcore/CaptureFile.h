#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace DevicesForge
{

	/** Extension sin punto. El dialogo de macOS la pide asi. */
	constexpr const char kCaptureExtension[] = "dfcapture";

	/*
	 * isCapturePath
	 *
	 * Pseudocodigo:
	 * 1. Comparar el final del path con ".dfcapture", sin importar mayusculas
	 */
	inline bool isCapturePath(const std::string& path)
	{
		const std::string suffix = std::string(".") + kCaptureExtension;
		if (path.size() < suffix.size())
			return false;
		for (size_t i = 0; i < suffix.size(); ++i)
		{
			char left = path[path.size() - suffix.size() + i];
			char right = suffix[i];
			if (left >= 'A' && left <= 'Z')
				left = static_cast<char>(left - 'A' + 'a');
			if (right >= 'A' && right <= 'Z')
				right = static_cast<char>(right - 'A' + 'a');
			if (left != right)
				return false;
		}
		return true;
	}

	/** Una captura publicable (.dfcapture). channels: 1, 2 o 4, misma longitud. */
	struct CaptureData
	{
		std::string name;
		std::string device;
		std::string author;
		std::string date;
		std::string notes;
		double sampleRate = 48000.0;
		float sendDb = 0.0f;
		/** Ganancia en banda (dB) antes de normalizar. 0 si no se normalizo. */
		float bandGainDb = 0.0f;
		bool bandNormalized = false;
		std::vector<std::vector<float>> channels;
	};

	/**
	 * CaptureFile
	 *
	 * Archivo .dfcapture: cabecera versionada, textos, IR float32 y CRC32.
	 */
	class CaptureFile
	{
	public:
		static bool save(const std::string& path, const CaptureData& data);
		static bool load(const std::string& path, CaptureData& data);
	};

	/*
	 * normalizeBandUnity
	 *
	 * Pseudocodigo:
	 * 1. Filtrar la IR y un impulso con pasaaltos 200 Hz y pasabajos 5 kHz
	 * 2. Medir la ganancia como relacion de RMS
	 * 3. Escalar la IR para que esa ganancia quede en 0 dB
	 * 4. Devolver la ganancia original en dB
	 */
	bool normalizeBandUnity(std::vector<float>& ir, double sampleRate, float& gainDb);

} // namespace DevicesForge
