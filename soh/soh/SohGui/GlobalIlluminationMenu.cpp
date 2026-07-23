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
            "Adds indirect environment light to the complete 3D scene. The stencil volumes affect map geometry "
            "and opaque actors together while preserving hard cel-shaded shadow edges."));

    mSohMenu->AddWidget(path, "Scene-Wide GI", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessEnabled);

    mSohMenu->AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessEnabled)
        .Callback([](WidgetInfo& info) {
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.BounceHeight"));
            // Remove the obsolete screen-overlay setting left by the first Android test build.
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Restores the scene-wide GI values to their mobile defaults."));

    mSohMenu->AddWidget(path, "Environment Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Strength of indirect sky and environment light applied to the visible map and actors.")
                     .Min(0.0f)
                     .Max(0.60f)
                     .DefaultValue(0.22f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Ground Bounce", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Strength of the warm indirect light reflected upward from the floor onto map surfaces "
                              "and actors.")
                     .Min(0.0f)
                     .Max(0.45f)
                     .DefaultValue(0.14f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Ground Bounce Height", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.BounceHeight"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Height in world units reached by the ground-bounce volume above the player's floor.")
                     .Min(0.0f)
                     .Max(1200.0f)
                     .DefaultValue(300.0f)
                     .Format("%.0f"));
}

static RegisterMenuInitFunc sGlobalIlluminationMenuInit(AddGlobalIlluminationMenu);

} // namespace SohGui
