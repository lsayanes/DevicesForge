#pragma once

#include <string>

namespace DevicesForge
{

	/** Diálogo nativo para elegir dónde guardar/abrir una IR (WAV float). */
	bool chooseSaveIRPath(std::string& outPath);
	bool chooseOpenIRPath(std::string& outPath);

} // DevicesForge
