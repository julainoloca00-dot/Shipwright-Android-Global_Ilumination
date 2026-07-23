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
            "Adds indirect light through the normal scene-lighting system. It affects illuminated map geometry "
            "and actors without drawing stencil boxes, screen overlays or extra shadow meshes."));

    mSohMenu->AddWidget(path, "Scene Lighting GI", WIDGET_SEPARATOR_TEXT).PreFunc(hideUnlessEnabled);

    mSohMenu->AddWidget(path, "Reset All to Defaults", WIDGET_BUTTON)
        .PreFunc(hideUnlessEnabled)
        .Callback([](WidgetInfo& info) {
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"));
            // Clear settings left by the two obsolete screen/stencil implementations.
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.BounceHeight"));
            CVarClear(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"));
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        })
        .Options(ButtonOptions().Tooltip("Restores the scene-light GI values to their mobile defaults."));

    mSohMenu->AddWidget(path, "Environment Intensity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Raises the environment-coloured ambient light used by the map and actors. This brightens "
                              "dark cel bands without drawing any visible volume.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.22f)
                     .IsPercentage());

    mSohMenu->AddWidget(path, "Ground Bounce", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"))
        .RaceDisable(false)
        .PreFunc(hideUnlessEnabled)
        .Options(FloatSliderOptions()
                     .Tooltip("Adds a subtle warm ambient lift plus a weak directional light coming from below. It "
                              "travels through the normal map and actor lighting path.")
                     .Min(0.0f)
                     .Max(1.0f)
                     .DefaultValue(0.14f)
                     .IsPercentage());
}

static RegisterMenuInitFunc sGlobalIlluminationMenuInit(AddGlobalIlluminationMenu);

} // namespace SohGui
