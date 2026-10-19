#pragma once

#include "EmulatorIDs.h"
#include "DFConstants.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <string>
#include <vector>

namespace DevicesForge
{
	namespace Emulator
	{

		/** Mensajes entre controller y procesador. */
		constexpr const char* MSG_LOAD_CAPTURE = "DFLoadCapture";
		constexpr const char* MSG_CAPTURE_NAME = "DFCaptureName";
		constexpr const char* ATTR_PATH = "Path";
		constexpr const char* ATTR_NAME = "Name";
		constexpr const char* ATTR_AUTHOR = "Author";

		/** Largo maximo del nombre guardado (bytes UTF-8). */
		constexpr Steinberg::int32 CAPTURE_NAME_MAX = 240;

		/** Texto UTF-8 en un mensaje. String::text8() del SDK usa la codificacion
		    del sistema y rompe rutas y nombres con tildes. */
		inline void setUtf8Attribute(Steinberg::Vst::IAttributeList* list, const char* id, const std::string& value)
		{
			const std::u16string wide = Steinberg::Vst::StringConvert::convert(value);
			list->setString(id, Steinberg::Vst::toTChar(wide));
		}

		inline bool getUtf8Attribute(Steinberg::Vst::IAttributeList* list, const char* id, std::string& value)
		{
			Steinberg::Vst::TChar buffer[1024] = {};
			if (list->getString(id, buffer, sizeof(buffer)) != Steinberg::kResultOk)
				return false;
			value = Steinberg::Vst::StringConvert::convert(buffer);
			return true;
		}

		/** Largo del nombre recortado sin partir un caracter UTF-8. */
		inline size_t captureNameLength(const std::string& name)
		{
			size_t n = std::min(name.size(), static_cast<size_t>(CAPTURE_NAME_MAX));
			while (n > 0 && n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80)
				--n;
			return n;
		}

		/** Lo que el Emulador guarda en el proyecto, sin la IR. */
		struct StoredState
		{
			float inputGain { 0.5f };
			float mix { 1.0f };
			float outputGain { 0.5f };
			float bypass { 0.0f };
			std::string captureName;
			std::string captureAuthor;
		};

		/** Texto recortado al maximo, sin cortar un caracter UTF-8 a la mitad. */
		inline void writeCappedText(Steinberg::IBStreamer& streamer, const std::string& text)
		{
			const auto n = static_cast<Steinberg::int32>(captureNameLength(text));
			streamer.writeInt32(n);
			for (Steinberg::int32 i = 0; i < n; ++i)
				streamer.writeInt8(static_cast<Steinberg::int8>(text[static_cast<size_t>(i)]));
		}

		/*
		 * readCappedText
		 *
		 * Pseudocodigo:
		 * 1. Si no hay mas datos y el campo es opcional, dejar el texto vacio
		 * 2. Leer el largo y los bytes
		 */
		inline bool readCappedText(Steinberg::IBStreamer& streamer, std::string& text, bool optional)
		{
			/* Un estado viejo puede no traer el campo */
			Steinberg::int32 n = 0;
			if (!streamer.readInt32(n))
			{
				text.clear();
				return optional;
			}
			if (n < 0 || n > CAPTURE_NAME_MAX)
				return false;

			/* Leo los bytes */
			text.assign(static_cast<size_t>(n), '\0');
			for (Steinberg::int32 i = 0; i < n; ++i)
			{
				Steinberg::int8 c = 0;
				if (!streamer.readInt8(c))
					return false;
				text[static_cast<size_t>(i)] = static_cast<char>(c);
			}
			return true;
		}

		/*
		 * writeState
		 *
		 * Pseudocodigo:
		 * 1. Escribir la version y los parametros
		 * 2. Escribir la IR (largo + muestras; 0 si no hay)
		 * 3. Escribir el nombre y el autor de la captura
		 */
		inline void writeState(Steinberg::IBStreamer& streamer, const StoredState& state, const std::vector<float>& ir)
		{
			/* Escribo version y parametros */
			streamer.writeInt32(STATE_VERSION);
			streamer.writeFloat(state.inputGain);
			streamer.writeFloat(state.mix);
			streamer.writeFloat(state.outputGain);
			streamer.writeFloat(state.bypass);

			/* Escribo la IR */
			streamer.writeInt32(static_cast<Steinberg::int32>(ir.size()));
			if (!ir.empty())
				streamer.writeFloatArray(ir.data(), static_cast<Steinberg::int32>(ir.size()));

			/* Escribo el nombre y el autor. El autor va al final: un estado viejo no lo trae. */
			writeCappedText(streamer, state.captureName);
			writeCappedText(streamer, state.captureAuthor);
		}

		/*
		 * readState
		 *
		 * Pseudocodigo:
		 * 1. Verificar la version
		 * 2. Leer los parametros
		 * 3. Leer la IR, o saltearla si ir es nullptr (controller)
		 * 4. Leer el nombre y, si esta, el autor
		 */
		inline bool readState(Steinberg::IBStreamer& streamer, StoredState& state, std::vector<float>* ir)
		{
			/* Verifico la version: un estado desconocido no se aplica */
			Steinberg::int32 version = 0;
			if (!streamer.readInt32(version) || version != STATE_VERSION)
				return false;

			/* Leo los parametros */
			if (!streamer.readFloat(state.inputGain) || !streamer.readFloat(state.mix) ||
				!streamer.readFloat(state.outputGain) || !streamer.readFloat(state.bypass))
				return false;

			/* Leo la IR o la salteo */
			Steinberg::int32 length = 0;
			if (!streamer.readInt32(length) || length < 0 || length > IR_STATE_MAX_SAMPLES)
				return false;
			if (ir)
			{
				ir->assign(static_cast<size_t>(length), 0.0f);
				if (length > 0 && !streamer.readFloatArray(ir->data(), length))
					return false;
			}
			else if (length > 0)
			{
				streamer.seek(static_cast<Steinberg::int64>(length) * static_cast<Steinberg::int64>(sizeof(float)),
							  Steinberg::kSeekCurrent);
			}

			/* Leo el nombre (obligatorio) y el autor (los estados viejos no lo traen) */
			if (!readCappedText(streamer, state.captureName, false))
				return false;
			return readCappedText(streamer, state.captureAuthor, true);
		}

	} // namespace Emulator
} // namespace DevicesForge
