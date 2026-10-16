#include "FileDialog.h"
#include "IRExporter.h"
#include "CaptureFile.h"

#include <string>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

namespace DevicesForge
{

	namespace
	{
		std::wstring utf8ToWide(const std::string& text)
		{
			if (text.empty())
				return {};
			const int wideCount = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
													  static_cast<int>(text.size()), nullptr, 0);
			if (wideCount <= 0)
				return {};
			std::wstring wide(static_cast<size_t>(wideCount), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
								wide.data(), wideCount);
			return wide;
		}

		std::string wideToUtf8(const wchar_t* wide)
		{
			if (!wide || wide[0] == L'\0')
				return {};
			const int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
			if (utf8Count <= 1)
				return {};
			std::string utf8(static_cast<size_t>(utf8Count - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), utf8Count, nullptr, nullptr);
			return utf8;
		}

		/*
		 * ensureLibraryDir
		 *
		 * Pseudocodigo:
		 * 1. Tomar Documents/DevicesForge/library
		 * 2. Crear cada segmento del path si no existe
		 */
		void ensureLibraryDir()
		{
			const std::string dir = IRExporter::defaultLibraryDirectory();
			std::string built;
			for (size_t i = 0; i < dir.size(); ++i)
			{
				built.push_back(dir[i]);
				if (dir[i] == '/' || dir[i] == '\\' || i + 1 == dir.size())
				{
					if (built.size() <= 3)
						continue;
					const std::wstring wide = utf8ToWide(built);
					CreateDirectoryW(wide.c_str(), nullptr);
				}
			}
		}

		bool runFileDialog(bool save, std::string& outPath)
		{
			ensureLibraryDir();

			const std::wstring initialDir = utf8ToWide(IRExporter::rememberedBrowseDirectory());
			wchar_t fileName[MAX_PATH] = L"IR.float.wav";

			const std::wstring captureExt = utf8ToWide(kCaptureExtension);
			std::wstring openFilter = L"IR (*.wav;*.";
			openFilter += captureExt;
			openFilter += L")";
			openFilter.push_back(L'\0');
			openFilter += L"*.wav;*.";
			openFilter += captureExt;
			openFilter.push_back(L'\0');
			openFilter.push_back(L'\0');

			OPENFILENAMEW ofn {};
			ofn.lStructSize = sizeof(ofn);
			ofn.lpstrFilter = save ? L"WAV (*.wav)\0*.wav\0\0" : openFilter.c_str();
			ofn.lpstrFile = fileName;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
			ofn.lpstrDefExt = L"wav";
			ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
			if (save)
			{
				ofn.lpstrTitle = L"Guardar IR";
				ofn.Flags |= OFN_OVERWRITEPROMPT;
				if (!GetSaveFileNameW(&ofn))
					return false;
			}
			else
			{
				ofn.lpstrTitle = L"Cargar IR";
				ofn.Flags |= OFN_FILEMUSTEXIST;
				if (!GetOpenFileNameW(&ofn))
					return false;
			}

			outPath = wideToUtf8(fileName);
			if (outPath.empty())
				return false;
			IRExporter::rememberBrowseFile(outPath);
			return true;
		}
	}

	bool chooseSaveIRPath(std::string& outPath)
	{
		return runFileDialog(true, outPath);
	}

	bool chooseOpenIRPath(std::string& outPath)
	{
		return runFileDialog(false, outPath);
	}

} // DevicesForge

#else

namespace DevicesForge
{

	bool chooseSaveIRPath(std::string& outPath)
	{
		outPath = IRExporter::defaultLibraryDirectory() + "/IR.float.wav";
		return true;
	}

	bool chooseOpenIRPath(std::string& outPath)
	{
		outPath = IRExporter::defaultLibraryDirectory() + "/IR.float.wav";
		return true;
	}

} // DevicesForge

#endif
