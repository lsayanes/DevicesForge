#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <string>

namespace Steinberg {
namespace Vst {

	class DevicesForgeEditor : public VSTGUIEditor, public VSTGUI::IControlListener
	{
	public:
		explicit DevicesForgeEditor(void* controller);
		~DevicesForgeEditor() override;

		bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
		void PLUGIN_API close() override;

		void valueChanged(VSTGUI::CControl* control) override;
		void controlBeginEdit(VSTGUI::CControl* control) override;
		void controlEndEdit(VSTGUI::CControl* control) override;

		VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) override;

	private:
		VSTGUI::CSlider* makeSlider(const VSTGUI::CRect& rect, int32_t tag, bool analog);
		VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& rect, const char* text);
		void addRow(VSTGUI::CFrame* host, float y, const char* name, int32_t tag,
					VSTGUI::CSlider*& slider, VSTGUI::CTextLabel*& readout, bool analog);
		void syncFromController();
		void updateReadout(int32_t tag, float normalized);

		VSTGUI::CSlider* sendSlider { nullptr };
		VSTGUI::CSlider* mixSlider { nullptr };
		VSTGUI::CSlider* gainSlider { nullptr };
		VSTGUI::CSlider* durationSlider { nullptr };
		VSTGUI::CSlider* inPeakMeter { nullptr };
		VSTGUI::CTextButton* generateButton { nullptr };
		VSTGUI::CTextButton* saveButton { nullptr };
		VSTGUI::CTextButton* loadButton { nullptr };
		VSTGUI::CTextLabel* sendReadout { nullptr };
		VSTGUI::CTextLabel* mixReadout { nullptr };
		VSTGUI::CTextLabel* gainReadout { nullptr };
		VSTGUI::CTextLabel* durationReadout { nullptr };
		VSTGUI::CTextLabel* inPeakReadout { nullptr };
		VSTGUI::CTextLabel* irLenLabel { nullptr };
		std::string shownIrLine;
	};

} // Vst
} // Steinberg
