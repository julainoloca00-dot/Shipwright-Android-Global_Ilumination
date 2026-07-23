#include "SohGui.hpp"
#include "UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

namespace SohGui {

extern std::shared_ptr<SohMenu> mSohMenu;
using namespace UIWidgets;

static void AddGlobalIlluminationMenu() {
    if (mSohMenu == nullptr) {
        return;
    }

    auto hideUnlessEnabled = [](WidgetInfo& info) {
        info.isHidden = !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"), 1);
    };

    WidgetPath path = { "Enhancements", "Global Illumination", SECTION_COLUMN_1 };
    mSohMenu->AddSidebarEntry("Enhancements", "Global Illumination", 1);

    mSohMenu->AddWidget(path, "Enable Global Illumination", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Enables lightweight mobile indirect lighting. The pass has fixed cost, uses no extra framebuffer, "
            "and does not blur cel-shaded shadow edges."));

    mSohMenu->AddWidget(path, "Mobile GI", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessEnabled);

    mSohMenu->AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessEnabled)
        .Callback([](WidgetInfo& info) {
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Restores the Android GI values to their performance-focused defaults."));

    mSohMenu->AddWidget(path, "Global Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Strength of the scene-wide indirect-light lift. Higher values brighten dark cel bands "
                              "but can reduce direct-light contrast.")
                     .Min(0.0f)
                     .Max(0.5f)
                     .DefaultValue(0.16f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Ground Bounce", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Strength of the warm indirect light rising from the lower part of the image.")
                     .Min(0.0f)
                     .Max(0.35f)
                     .DefaultValue(0.10f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Ground Coverage", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("How much of the lower screen receives the ground-bounce gradient. This changes only "
                              "the gradient placement, not its fixed rendering cost.")
                     .Min(0.20f)
                     .Max(0.85f)
                     .DefaultValue(0.52f)
                     .IsPercentage());
}

static RegisterMenuInitFunc sGlobalIlluminationMenuInit(AddGlobalIlluminationMenu);

} // namespace SohGui
