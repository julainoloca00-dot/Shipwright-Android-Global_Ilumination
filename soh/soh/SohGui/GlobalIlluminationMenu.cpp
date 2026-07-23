#include "SohGui.hpp"
#include "UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
using namespace UIWidgets;

static void AddScreenSpaceSunShadowMenu() {
    if (mSohMenu == nullptr) {
        return;
    }

    auto hideUnlessEnabled = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Enabled"), 1);
    };

    WidgetPath path = { "Enhancements", "Sun Shadows", SECTION_COLUMN_1 };
    mSohMenu->AddSidebarEntry("Enhancements", "Sun Shadows", 1);

    mSohMenu->AddWidget(path, "Enable Screen-Space Sun Shadows", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Uses the previous frame depth buffer to approximate direct shadows from the dominant sun or moon light. "
            "It affects visible map geometry and actors without rendering the scene twice."));

    mSohMenu->AddWidget(path, "Direct Lighting Shadows", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessEnabled);

    mSohMenu->AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessEnabled)
        .Callback([](WidgetInfo& info) {
            CVarClear(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Strength"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Length"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Bias"));
            // Remove every setting left by the discarded GI experiments.
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.BounceHeight"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Restores the mobile sun-shadow defaults and clears obsolete GI settings."));

    mSohMenu->AddWidget(path, "Shadow Strength", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Strength"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How strongly direct sunlight is removed where the depth ray finds an occluder.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.70f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Shadow Length", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Length"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Maximum screen-space distance traced toward the sun. Larger values create longer shadows "
                              "but can reveal more screen-space limitations.")
                     .Min(4.0f)
                     .Max(96.0f)
                     .DefaultValue(40.0f)
                     .Format("%.0f px"));

    mSohMenu->AddWidget(path, "Depth Bias", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Bias"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Prevents a surface from shadowing itself. Increase only if dark speckles appear.")
                     .Min(0.0002f)
                     .Max(0.0100f)
                     .DefaultValue(0.0018f)
                     .Format("%.4f"));
}

static RegisterMenuInitFunc sScreenSpaceSunShadowMenuInit(AddScreenSpaceSunShadowMenu);

} // namespace SohGui
