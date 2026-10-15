#include "FileDialog.h"
#include "../dsp/IRExporter.h"

#import <AppKit/AppKit.h>

#include <string>

namespace DevicesForge
{

	namespace
	{
		NSURL* browseDirectoryURL()
		{
			const std::string dir = IRExporter::rememberedBrowseDirectory();
			NSString* path = [NSString stringWithUTF8String:dir.c_str()];
			[[NSFileManager defaultManager] createDirectoryAtPath:path
									  withIntermediateDirectories:YES
													   attributes:nil
															error:nil];
			return [NSURL fileURLWithPath:path];
		}

		std::string pathFromURL(NSURL* url)
		{
			if (!url)
				return {};
			const char* utf8 = [[url path] UTF8String];
			return utf8 ? std::string(utf8) : std::string();
		}
	}

	bool chooseSaveIRPath(std::string& outPath)
	{
		@autoreleasepool
		{
			NSSavePanel* panel = [NSSavePanel savePanel];
			[panel setAllowedFileTypes:@[ @"wav" ]];
			[panel setCanCreateDirectories:YES];
			[panel setNameFieldStringValue:@"IR.float.wav"];
			[panel setDirectoryURL:browseDirectoryURL()];
			[panel setTitle:@"Guardar IR"];
			if ([panel runModal] != NSModalResponseOK)
				return false;
			outPath = pathFromURL([panel URL]);
			if (outPath.empty())
				return false;
			IRExporter::rememberBrowseFile(outPath);
			return true;
		}
	}

	bool chooseOpenIRPath(std::string& outPath)
	{
		@autoreleasepool
		{
			NSOpenPanel* panel = [NSOpenPanel openPanel];
			[panel setAllowedFileTypes:@[ @"wav" ]];
			[panel setCanChooseFiles:YES];
			[panel setCanChooseDirectories:NO];
			[panel setAllowsMultipleSelection:NO];
			[panel setDirectoryURL:browseDirectoryURL()];
			[panel setTitle:@"Cargar IR"];
			if ([panel runModal] != NSModalResponseOK)
				return false;
			outPath = pathFromURL([panel URL]);
			if (outPath.empty())
				return false;
			IRExporter::rememberBrowseFile(outPath);
			return true;
		}
	}

} // DevicesForge
