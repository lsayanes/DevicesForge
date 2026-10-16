#include "PluginEditor.h"
#include "DevicesForge.h"
#include "version.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"

#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace Steinberg {
namespace Vst {

	namespace
	{
		constexpr CCoord kWidth = 520;
		constexpr CCoord kHeight = 360;
		const CColor kBg { 28, 30, 34, 255 };
		const CColor kText { 220, 222, 226, 255 };
		const CColor kMuted { 140, 144, 150, 255 };
		const CColor kAccent { 230, 148, 64, 255 };
		const CColor kTeal { 72, 168, 158, 255 };
		const CColor kTrack { 22, 24, 26, 255 };
	}

	DevicesForgeEditor::DevicesForgeEditor(void* controller)
		: VSTGUIEditor(controller)
	{
		ViewRect vr(0, 0, static_cast<int32>(kWidth), static_cast<int32>(kHeight));
		setRect(vr);
		setIdleRate(40);
	}

	DevicesForgeEditor::~DevicesForgeEditor() = default;

	CSlider* DevicesForgeEditor::makeSlider(const CRect& rect, int32_t tag, bool analog)
	{
		const int32_t travel = static_cast<int32_t>(rect.getWidth()) - 4;
		auto* slider = new CSlider(rect, this, tag, 0, travel, nullptr, nullptr);
		slider->setStyle({ CSlider::kHorizontal, CSlider::kLeft });
		slider->setDrawStyle(CSlider::kDrawFrame | CSlider::kDrawBack | CSlider::kDrawValue);
		slider->setBackColor(kTrack);
		slider->setFrameColor(CColor(70, 74, 80, 255));
		slider->setValueColor(analog ? kAccent : kTeal);
		slider->setFrameWidth(1);
		slider->setMouseEnabled(analog);
		slider->setWheelInc(0.02f);
		return slider;
	}

	CTextLabel* DevicesForgeEditor::makeLabel(const CRect& rect, const char* text)
	{
		auto* label = new CTextLabel(rect, text);
		label->setFont(kNormalFontSmall);
		label->setFontColor(kText);
		label->setBackColor(kTransparentCColor);
		label->setFrameColor(kTransparentCColor);
		label->setHoriAlign(kLeftText);
		return label;
	}

	void DevicesForgeEditor::addRow(CFrame* host, float y, const char* name, int32_t tag,
									CSlider*& slider, CTextLabel*& readout, bool analog)
	{
		host->addView(makeLabel(CRect(20, y, 90, y + 22), name));
		slider = makeSlider(CRect(96, y + 4, 380, y + 20), tag, analog);
		host->addView(slider);
		readout = makeLabel(CRect(390, y, 500, y + 22), "");
		readout->setHoriAlign(kRightText);
		host->addView(readout);
	}

	/*
	 * open
	 *
	 * Pseudocodigo:
	 * 1. Crear el frame
	 * 2. Armar filas de toma y emulacion
	 * 3. Sincronizar controles con el controller
	 */
	bool PLUGIN_API DevicesForgeEditor::open(void* parent, const PlatformType& platformType)
	{
		auto* newFrame = new CFrame(CRect(0, 0, kWidth, kHeight), this);
		if (!newFrame->open(parent, platformType))
		{
			newFrame->forget();
			return false;
		}
		newFrame->setBackgroundColor(kBg);

		newFrame->addView(makeLabel(CRect(20, 12, 280, 34), "DevicesForge"));
		auto* version = makeLabel(CRect(300, 14, 500, 32), FULL_VERSION_STR);
		version->setHoriAlign(kRightText);
		version->setFontColor(kMuted);
		newFrame->addView(version);

		auto* takeHead = makeLabel(CRect(20, 44, 200, 62), "TOMA");
		takeHead->setFontColor(kAccent);
		newFrame->addView(takeHead);

		addRow(newFrame, 68, "Send", static_cast<int32_t>(DevicesForge::PluginParamIDs::SEND),
			   sendSlider, sendReadout, true);
		addRow(newFrame, 96, "InPeak", static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK),
			   inPeakMeter, inPeakReadout, false);
		addRow(newFrame, 124, "Duration",
			   static_cast<int32_t>(DevicesForge::PluginParamIDs::SIGNAL_DURATION),
			   durationSlider, durationReadout, true);

		generateButton = new CTextButton(CRect(20, 156, 160, 186), this,
										 static_cast<int32_t>(DevicesForge::PluginParamIDs::GENERATE),
										 "Generate", CTextButton::kOnOffStyle);
		generateButton->setRoundRadius(3);
		generateButton->setTextColor(kText);
		generateButton->setFrameColor(kAccent);
		newFrame->addView(generateButton);

		auto* emuHead = makeLabel(CRect(20, 204, 200, 222), "EMULACION");
		emuHead->setFontColor(kTeal);
		newFrame->addView(emuHead);

		addRow(newFrame, 228, "Mix", static_cast<int32_t>(DevicesForge::PluginParamIDs::MIX),
			   mixSlider, mixReadout, true);
		addRow(newFrame, 256, "Gain", static_cast<int32_t>(DevicesForge::PluginParamIDs::OUTPUT_GAIN),
			   gainSlider, gainReadout, true);

		irLenLabel = makeLabel(CRect(20, 286, 500, 306), "IR  —");
		irLenLabel->setFontColor(kMuted);
		newFrame->addView(irLenLabel);

		saveButton = new CTextButton(CRect(20, 318, 150, 346), this,
									 static_cast<int32_t>(DevicesForge::PluginParamIDs::SAVE_IR),
									 "Save IR", CTextButton::kKickStyle);
		saveButton->setRoundRadius(3);
		newFrame->addView(saveButton);

		loadButton = new CTextButton(CRect(166, 318, 296, 346), this,
									 static_cast<int32_t>(DevicesForge::PluginParamIDs::LOAD_IR),
									 "Load IR", CTextButton::kKickStyle);
		loadButton->setRoundRadius(3);
		newFrame->addView(loadButton);

		frame = newFrame;
		syncFromController();
		return true;
	}

	void PLUGIN_API DevicesForgeEditor::close()
	{
		if (frame)
		{
			frame->forget();
			frame = nullptr;
		}
		sendSlider = mixSlider = gainSlider = durationSlider = inPeakMeter = nullptr;
		generateButton = saveButton = loadButton = nullptr;
		sendReadout = mixReadout = gainReadout = durationReadout = inPeakReadout = irLenLabel = nullptr;
	}

	void DevicesForgeEditor::controlBeginEdit(CControl* control)
	{
		if (!control || !getController())
			return;
		const int32_t tag = control->getTag();
		if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK))
			return;
		getController()->beginEdit(static_cast<ParamID>(tag));
	}

	void DevicesForgeEditor::controlEndEdit(CControl* control)
	{
		if (!control || !getController())
			return;
		const int32_t tag = control->getTag();
		if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK))
			return;
		getController()->endEdit(static_cast<ParamID>(tag));
	}

	void DevicesForgeEditor::valueChanged(CControl* control)
	{
		if (!control || !getController())
			return;

		const int32_t tag = control->getTag();
		if (tag < 0)
			return;
		if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK))
			return;

		const ParamID id = static_cast<ParamID>(tag);
		const ParamValue value = control->getValue();
		getController()->setParamNormalized(id, value);
		getController()->performEdit(id, value);
		updateReadout(tag, static_cast<float>(value));
	}

	CMessageResult DevicesForgeEditor::notify(CBaseObject* sender, const char* message)
	{
		if (message == CVSTGUITimer::kMsgTimer)
			syncFromController();
		return VSTGUIEditor::notify(sender, message);
	}

	void DevicesForgeEditor::syncFromController()
	{
		EditController* controller = getController();
		if (!controller)
			return;

		auto apply = [controller](CControl* control, ParamID id) {
			if (!control)
				return;
			const float value = static_cast<float>(controller->getParamNormalized(id));
			if (std::abs(control->getValue() - value) > 0.0005f)
			{
				control->setValue(value);
				control->setDirty();
			}
		};

		apply(sendSlider, DevicesForge::PluginParamIDs::SEND);
		apply(mixSlider, DevicesForge::PluginParamIDs::MIX);
		apply(gainSlider, DevicesForge::PluginParamIDs::OUTPUT_GAIN);
		apply(durationSlider, DevicesForge::PluginParamIDs::SIGNAL_DURATION);
		apply(inPeakMeter, DevicesForge::PluginParamIDs::INPUT_PEAK);
		apply(generateButton, DevicesForge::PluginParamIDs::GENERATE);

		if (sendSlider)
			updateReadout(static_cast<int32_t>(DevicesForge::PluginParamIDs::SEND), sendSlider->getValue());
		if (mixSlider)
			updateReadout(static_cast<int32_t>(DevicesForge::PluginParamIDs::MIX), mixSlider->getValue());
		if (gainSlider)
			updateReadout(static_cast<int32_t>(DevicesForge::PluginParamIDs::OUTPUT_GAIN),
						  gainSlider->getValue());
		if (durationSlider)
			updateReadout(static_cast<int32_t>(DevicesForge::PluginParamIDs::SIGNAL_DURATION),
						  durationSlider->getValue());
		if (inPeakMeter)
			updateReadout(static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK),
						  inPeakMeter->getValue());

		if (irLenLabel)
		{
			const float norm = static_cast<float>(
				controller->getParamNormalized(DevicesForge::PluginParamIDs::IR_LENGTH_MS));
			const float irMs = norm * DevicesForge::IR_LENGTH_METER_MAX_MS;
			char text[64];
			if (irMs < 0.5f)
				std::snprintf(text, sizeof(text), "IR  —  sin muestra (passthrough)");
			else
				std::snprintf(text, sizeof(text), "IR  %.0f ms", static_cast<double>(irMs));
			irLenLabel->setText(text);
			irLenLabel->setFontColor(irMs < 0.5f ? kMuted : kTeal);
		}
	}

	void DevicesForgeEditor::updateReadout(int32_t tag, float normalized)
	{
		char text[32] = {};
		CTextLabel* label = nullptr;

		if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::SEND) ||
			tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::OUTPUT_GAIN))
		{
			const float db = normalized * 24.0f - 12.0f;
			std::snprintf(text, sizeof(text), "%+.1f dB", static_cast<double>(db));
			label = (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::SEND)) ? sendReadout
																					 : gainReadout;
		}
		else if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::MIX))
		{
			std::snprintf(text, sizeof(text), "%.0f %%", static_cast<double>(normalized * 100.0f));
			label = mixReadout;
		}
		else if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::SIGNAL_DURATION))
		{
			const float seconds = DevicesForge::normalizedToDuration(normalized);
			std::snprintf(text, sizeof(text), "%.1f s", static_cast<double>(seconds));
			label = durationReadout;
		}
		else if (tag == static_cast<int32_t>(DevicesForge::PluginParamIDs::INPUT_PEAK))
		{
			const float db = DevicesForge::METER_FLOOR_DB +
							 normalized * (0.0f - DevicesForge::METER_FLOOR_DB);
			std::snprintf(text, sizeof(text), "%.0f dB", static_cast<double>(db));
			label = inPeakReadout;
		}

		if (label)
			label->setText(text);
	}

} // Vst
} // Steinberg
