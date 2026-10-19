#include "OfflineCapturer.h"
#include "WavIO.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

	void printUsage()
	{
		std::cerr
			<< "dfcapture make-sweep [--rate 48000] [--duration 5] [--repeats 3] -o sweep.wav\n"
			<< "dfcapture analyze grabacion.wav -o equipo.dfcapture [--name ...] [--author ...]\n"
			<< "                    [--device ...] [--notes ...] [--send-db 0]\n"
			<< "                    [--duration 5] [--repeats 3]\n"
			<< "\n"
			<< "make-sweep escribe silencio + marca + N barridos + marca + silencio.\n"
			<< "analyze tiene que usar el mismo duration y repeats. El rate sale del WAV.\n"
			<< "Corrige la velocidad media de la cinta. No corrige wow ni flutter.\n"
			<< "La cola entre barridos es 0,5 s: una IR mas larga se mezcla con el siguiente.\n"
			<< "analyze tambien escribe nombre.informe.html, con el grafico de la toma.\n";
	}

	bool takeArg(int& index, int argc, char** argv, std::string& value)
	{
		if (index + 1 >= argc)
			return false;
		value = argv[++index];
		return !value.empty();
	}

	bool parseDouble(const std::string& text, double& value)
	{
		char* end = nullptr;
		value = std::strtod(text.c_str(), &end);
		return end != text.c_str() && end && *end == '\0';
	}

	bool parseInt(const std::string& text, int& value)
	{
		char* end = nullptr;
		long parsed = std::strtol(text.c_str(), &end, 10);
		if (end == text.c_str() || !end || *end != '\0')
			return false;
		value = static_cast<int>(parsed);
		return true;
	}

	std::string qualityPathFor(const std::string& capturePath)
	{
		const auto dot = capturePath.find_last_of('.');
		const auto slash = capturePath.find_last_of("/\\");
		if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
			return capturePath.substr(0, dot) + ".quality.txt";
		return capturePath + ".quality.txt";
	}

	int makeSweep(int argc, char** argv)
	{
		DevicesForge::SweepSpec spec;
		std::string output;
		for (int i = 0; i < argc; ++i)
		{
			const std::string arg = argv[i];
			std::string value;
			if (arg == "--rate")
			{
				if (!takeArg(i, argc, argv, value) || !parseDouble(value, spec.sampleRate))
					return 1;
			}
			else if (arg == "--duration")
			{
				double seconds = 0.0;
				if (!takeArg(i, argc, argv, value) || !parseDouble(value, seconds))
					return 1;
				spec.durationSeconds = static_cast<float>(seconds);
			}
			else if (arg == "--repeats")
			{
				int repeats = 0;
				if (!takeArg(i, argc, argv, value) || !parseInt(value, repeats))
					return 1;
				spec.repeats = repeats;
			}
			else if (arg == "-o" || arg == "--output")
			{
				if (!takeArg(i, argc, argv, output))
					return 1;
			}
			else
			{
				std::cerr << "opcion desconocida: " << arg << "\n";
				return 1;
			}
		}

		if (output.empty())
		{
			std::cerr << "falta -o sweep.wav\n";
			return 1;
		}

		std::vector<float> samples;
		DevicesForge::SweepLayout layout;
		if (!DevicesForge::OfflineCapturer::makeSweep(spec, samples, layout))
		{
			std::cerr << "no pude armar el barrido (rate, duration 0.5..5 s, repeats 1..8)\n";
			return 1;
		}

		DevicesForge::WavBuffer wav;
		wav.sampleRate = spec.sampleRate;
		wav.channels.assign(1, samples);
		if (!DevicesForge::WavIO::save(output, wav, 32))
		{
			std::cerr << "no pude escribir " << output << "\n";
			return 1;
		}

		std::cout << output << "\n"
				  << "rate=" << layout.sampleRate << "\n"
				  << "duration=" << spec.durationSeconds << "\n"
				  << "repeats=" << layout.repeats << "\n"
				  << "muestras=" << samples.size() << "\n";
		return 0;
	}

	int analyze(int argc, char** argv)
	{
		if (argc < 1)
			return 1;

		const std::string input = argv[0];
		DevicesForge::AnalyzeOptions options;
		std::string output;
		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = argv[i];
			std::string value;
			if (arg == "--duration")
			{
				double seconds = 0.0;
				if (!takeArg(i, argc, argv, value) || !parseDouble(value, seconds))
					return 1;
				options.sweep.durationSeconds = static_cast<float>(seconds);
			}
			else if (arg == "--repeats")
			{
				int repeats = 0;
				if (!takeArg(i, argc, argv, value) || !parseInt(value, repeats))
					return 1;
				options.sweep.repeats = repeats;
			}
			else if (arg == "--name")
			{
				if (!takeArg(i, argc, argv, options.name))
					return 1;
			}
			else if (arg == "--author")
			{
				if (!takeArg(i, argc, argv, options.author))
					return 1;
			}
			else if (arg == "--device")
			{
				if (!takeArg(i, argc, argv, options.device))
					return 1;
			}
			else if (arg == "--notes")
			{
				if (!takeArg(i, argc, argv, options.notes))
					return 1;
			}
			else if (arg == "--send-db")
			{
				double db = 0.0;
				if (!takeArg(i, argc, argv, value) || !parseDouble(value, db))
					return 1;
				options.sendDb = static_cast<float>(db);
			}
			else if (arg == "-o" || arg == "--output")
			{
				if (!takeArg(i, argc, argv, output))
					return 1;
			}
			else
			{
				std::cerr << "opcion desconocida: " << arg << "\n";
				return 1;
			}
		}

		if (output.empty())
		{
			std::cerr << "falta -o equipo.dfcapture\n";
			return 1;
		}

		DevicesForge::WavBuffer wav;
		if (!DevicesForge::WavIO::load(input, wav) || wav.channels.empty() || wav.channels[0].empty())
		{
			std::cerr << "no pude leer " << input << "\n";
			return 1;
		}

		DevicesForge::CaptureData capture;
		DevicesForge::QualityReport report;
		const std::vector<float>& channel = wav.channels[0];
		if (!DevicesForge::OfflineCapturer::analyze(channel.data(), static_cast<int32_t>(channel.size()),
													wav.sampleRate, options, capture, report))
		{
			std::cerr << report.text;
			return 1;
		}

		if (capture.name.empty())
		{
			const auto slash = output.find_last_of("/\\");
			const std::string base = (slash == std::string::npos) ? output : output.substr(slash + 1);
			const auto dot = base.find_last_of('.');
			capture.name = (dot == std::string::npos) ? base : base.substr(0, dot);
		}

		if (!DevicesForge::CaptureFile::save(output, capture))
		{
			std::cerr << "no pude escribir " << output << "\n";
			return 1;
		}

		const std::string qualityPath = qualityPathFor(output);
		std::ofstream quality(qualityPath);
		quality << report.text;
		if (!quality)
		{
			std::cerr << "no pude escribir " << qualityPath << "\n";
			return 1;
		}

		const std::string htmlPath = DevicesForge::reportHtmlPath(output);
		if (!DevicesForge::OfflineCapturer::writeHtmlReport(htmlPath, capture, report))
		{
			std::cerr << "no pude escribir " << htmlPath << "\n";
			return 1;
		}

		std::cout << report.text << "informe=" << htmlPath << "\n";
		return 0;
	}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printUsage();
		return 1;
	}

	const std::string command = argv[1];
	if (command == "-h" || command == "--help")
	{
		printUsage();
		return 0;
	}
	if (command == "make-sweep")
		return makeSweep(argc - 2, argv + 2);
	if (command == "analyze")
	{
		if (argc < 3)
		{
			printUsage();
			return 1;
		}
		return analyze(argc - 2, argv + 2);
	}

	std::cerr << "comando desconocido: " << command << "\n";
	printUsage();
	return 1;
}
