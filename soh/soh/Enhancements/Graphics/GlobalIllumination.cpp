// Lightweight scene-wide indirect lighting for Android/OpenGL ES.
//
// This implementation deliberately does not draw geometry, use stencil volumes, sample the framebuffer,
// or run a post-process. Instead it temporarily modifies OoT's own scene lighting while the 3D world is
// rendered: the ambient term receives environment-coloured indirect light, and the weaker of the two
// environment directional lights becomes a subtle warm bounce coming from below. The original lighting is
// restored before the HUD and before the next update. Because this travels through the normal N64 lighting
// path, it affects illuminated map geometry and actors together without visible boxes or screen-space bands.

#include <libultraship/bridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"

extern PlayState* gPlayState;
}

static constexpr f32 kDefaultIntensity = 0.22f;
static constexpr f32 kDefaultGroundBounce = 0.14f;

// The GI is applied only between OnPlayDrawBegin and OnPlayDrawEnd.
static bool sGiApplied = false;
static PlayState* sGiPlay = nullptr;
static u8 sOriginalAmbient[3] = { 0, 0, 0 };
static LightInfo* sModifiedDirectional = nullptr;
static LightInfo sOriginalDirectional;

static f32 GiClampF(f32 value, f32 minValue, f32 maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static u8 GiClampByte(f32 value) {
    return static_cast<u8>(GiClampF(value, 0.0f, 255.0f) + 0.5f);
}

static s32 GiDirectionalLuminance(const LightInfo* light) {
    return static_cast<s32>(light->params.dir.color[0]) + static_cast<s32>(light->params.dir.color[1]) +
           static_cast<s32>(light->params.dir.color[2]);
}

static void RestoreGlobalIllumination() {
    if (!sGiApplied) {
        return;
    }

    if (sGiPlay != nullptr) {
        sGiPlay->lightCtx.ambientColor[0] = sOriginalAmbient[0];
        sGiPlay->lightCtx.ambientColor[1] = sOriginalAmbient[1];
        sGiPlay->lightCtx.ambientColor[2] = sOriginalAmbient[2];
    }

    if (sModifiedDirectional != nullptr) {
        *sModifiedDirectional = sOriginalDirectional;
    }

    sGiApplied = false;
    sGiPlay = nullptr;
    sModifiedDirectional = nullptr;
}

static void ApplyGlobalIllumination() {
    // Recover cleanly if a previous frame left the draw hook early.
    RestoreGlobalIllumination();

    PlayState* play = gPlayState;
    if (play == nullptr || play->state.gfxCtx == nullptr) {
        return;
    }

    const f32 intensity =
        GiClampF(CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"), kDefaultIntensity), 0.0f,
                 1.0f);
    const f32 groundBounce =
        GiClampF(CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"), kDefaultGroundBounce),
                 0.0f, 1.0f);

    if (intensity <= 0.0001f && groundBounce <= 0.0001f) {
        return;
    }

    sGiPlay = play;
    sOriginalAmbient[0] = play->lightCtx.ambientColor[0];
    sOriginalAmbient[1] = play->lightCtx.ambientColor[1];
    sOriginalAmbient[2] = play->lightCtx.ambientColor[2];

    LightInfo* light1 = &play->envCtx.dirLight1;
    LightInfo* light2 = &play->envCtx.dirLight2;
    const s32 luminance1 = GiDirectionalLuminance(light1);
    const s32 luminance2 = GiDirectionalLuminance(light2);
    const LightInfo* dominant = (luminance2 > luminance1) ? light2 : light1;

    // Preserve the dominant sun/moon light. The weaker environment light is temporarily repurposed as a
    // low-energy bounce, guaranteeing that ToonLighting's key-light selection remains the sun/moon rather
    // than suddenly flipping below the actors.
    sModifiedDirectional = (dominant == light1) ? light2 : light1;
    sOriginalDirectional = *sModifiedDirectional;

    const f32 dominantAverage = GiDirectionalLuminance(dominant) / 3.0f;
    static constexpr f32 warmGround[3] = { 1.00f, 0.78f, 0.54f };

    u8 bounceColor[3] = { 0, 0, 0 };
    for (s32 channel = 0; channel < 3; ++channel) {
        const f32 originalAmbient = sOriginalAmbient[channel];
        const f32 fog = play->lightCtx.fogColor[channel];
        const f32 key = dominant->params.dir.color[channel];

        // Environment-coloured sky fill. This is additive, so it raises dark cel bands instead of replacing
        // the material colour or painting a translucent rectangle over the image.
        const f32 environmentHue = (originalAmbient * 0.48f) + (key * 0.34f) + (fog * 0.18f);
        const f32 environmentLift = intensity * (18.0f + (environmentHue * 0.30f));

        // Warm ground contribution is included in ambient so horizontal floors and scenery also receive it;
        // the directional term below adds normal-dependent shaping to walls, actors and undersides.
        const f32 groundHue = (originalAmbient * 0.42f) + (fog * 0.18f) + (key * warmGround[channel] * 0.40f);
        const f32 groundLift = groundBounce * (10.0f + (groundHue * 0.22f));

        play->lightCtx.ambientColor[channel] = GiClampByte(originalAmbient + environmentLift + groundLift);

        // Keep the bounce weaker than the dominant light at every time of day, so it cannot become the toon key.
        const f32 bounceLimit = GiClampF((dominantAverage * 0.32f) + 8.0f, 8.0f, 72.0f);
        bounceColor[channel] = GiClampByte((groundHue / 255.0f) * groundBounce * bounceLimit);
    }

    // A slightly tilted light from below reaches vertical scenery as well as downward-facing surfaces. It is
    // intentionally weak; the ambient lift provides the broad GI while this term only gives the bounce shape.
    Lights_DirectionalSetInfo(sModifiedDirectional, 24, -120, 16, bounceColor[0], bounceColor[1], bounceColor[2]);

    sGiApplied = true;
}

static void RegisterGlobalIllumination() {
    // A CVar change can rebuild the hooks between frames. Restore first so disabling GI never leaves modified
    // scene-light values behind.
    RestoreGlobalIllumination();

    const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"), 1) != 0;
    COND_HOOK(OnPlayDrawBegin, enabled, ApplyGlobalIllumination);
    COND_HOOK(OnPlayDrawEnd, enabled, RestoreGlobalIllumination);
}

static RegisterShipInitFunc sGlobalIlluminationInit(RegisterGlobalIllumination,
                                                    { CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled") });
