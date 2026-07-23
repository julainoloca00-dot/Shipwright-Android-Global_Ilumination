// Lightweight mobile global illumination for the Android/OpenGL ES build.
//
// This is intentionally a fixed-cost screen-space approximation. It runs after the 3D world and actors,
// but before the HUD, and uses the game's own Fast3D translucent display list. No extra framebuffer,
// depth readback, stencil buffer, ray tracing, blur, temporal history, or per-actor work is required.
// Hard cel-shaded shadow edges remain hard because the pass only blends color; it never filters pixels.

#include <libultraship/bridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
// OPEN_DISPS/CLOSE_DISPS use the frame-interpolation declarations. Keep this before the game headers.
#include "soh/frame_interpolation.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

extern PlayState* gPlayState;
}

static constexpr f32 kDefaultIntensity = 0.16f;
static constexpr f32 kDefaultGroundBounce = 0.10f;
static constexpr f32 kDefaultGroundCoverage = 0.52f;
static constexpr s32 kGroundGradientBands = 8;

static f32 GiClampF(f32 value, f32 minValue, f32 maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static u8 GiToByte(f32 value) {
    return static_cast<u8>((GiClampF(value, 0.0f, 1.0f) * 255.0f) + 0.5f);
}

static u8 GiIntensityToAlpha(f32 intensity, f32 scale) {
    return GiToByte(GiClampF(intensity, 0.0f, 1.0f) * scale);
}

static void GiComputeIndirectColors(PlayState* play, u8 skyColor[3], u8 groundColor[3]) {
    const LightInfo* sun = &play->envCtx.dirLight1;
    const LightInfo* moon = &play->envCtx.dirLight2;

    const s32 sunLum = sun->params.dir.color[0] + sun->params.dir.color[1] + sun->params.dir.color[2];
    const s32 moonLum = moon->params.dir.color[0] + moon->params.dir.color[1] + moon->params.dir.color[2];
    const LightInfo* dominant = (moonLum > sunLum) ? moon : sun;

    const f32 daylight =
        GiClampF((dominant->params.dir.color[0] + dominant->params.dir.color[1] + dominant->params.dir.color[2]) /
                     (3.0f * 255.0f),
                 0.0f, 1.0f);

    static constexpr f32 warmGround[3] = { 1.0f, 0.78f, 0.55f };
    f32 rawSky[3];
    f32 rawGround[3];
    f32 maxSky = 0.0001f;
    f32 maxGround = 0.0001f;

    for (s32 i = 0; i < 3; ++i) {
        const f32 ambient = play->lightCtx.ambientColor[i] / 255.0f;
        const f32 fog = play->lightCtx.fogColor[i] / 255.0f;
        const f32 key = dominant->params.dir.color[i] / 255.0f;

        rawSky[i] = (ambient * 0.52f) + (key * 0.34f) + (fog * 0.14f);
        rawGround[i] = (ambient * 0.40f) + (key * warmGround[i] * 0.42f) + (fog * 0.18f);

        if (rawSky[i] > maxSky) {
            maxSky = rawSky[i];
        }
        if (rawGround[i] > maxGround) {
            maxGround = rawGround[i];
        }
    }

    // Keep the overlay bright enough to lift dark areas rather than muddying the image. The normalized
    // environment colors provide the hue; the high base value makes this behave like indirect light.
    const f32 skyBase = 0.55f + (0.10f * daylight);
    const f32 groundBase = 0.50f + (0.08f * daylight);

    for (s32 i = 0; i < 3; ++i) {
        const f32 sky = skyBase + ((1.0f - skyBase) * (rawSky[i] / maxSky));
        const f32 ground = groundBase + ((1.0f - groundBase) * (rawGround[i] / maxGround));
        skyColor[i] = GiToByte(sky);
        groundColor[i] = GiToByte(ground);
    }
}

static void GiDrawComposite(PlayState* play, const u8 skyColor[3], u8 skyAlpha, const u8 groundColor[3], u8 groundAlpha,
                            f32 groundCoverage) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    if (gfxCtx == NULL || (skyAlpha == 0 && groundAlpha == 0)) {
        return;
    }

    OPEN_DISPS(gfxCtx);

    // Gfx_SetupDL_57 is the same translucent screen-fill path used by Environment_FillScreen.
    // Use only POLY_XLU so the already-rendered opaque and translucent world receives one final blend.
    POLY_XLU_DISP = Gfx_SetupDL_57(POLY_XLU_DISP);
    gDPSetAlphaDither(POLY_XLU_DISP++, G_AD_DISABLE);
    gDPSetColorDither(POLY_XLU_DISP++, G_CD_DISABLE);

    if (skyAlpha > 0) {
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, skyColor[0], skyColor[1], skyColor[2], skyAlpha);
        gDPFillRectangle(POLY_XLU_DISP++, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    }

    if (groundAlpha > 0) {
        const f32 coverage = GiClampF(groundCoverage, 0.20f, 0.85f);
        const s32 startY = static_cast<s32>(SCREEN_HEIGHT * (1.0f - coverage));
        const s32 height = SCREEN_HEIGHT - startY;

        // A few non-overlapping bands approximate a vertical ground-bounce gradient. The cost is fixed
        // (eight tiny rectangles) and does not depend on scene complexity, actor count, or resolution scale.
        for (s32 band = 0; band < kGroundGradientBands; ++band) {
            const s32 y0 = startY + ((height * band) / kGroundGradientBands);
            s32 y1 = startY + ((height * (band + 1)) / kGroundGradientBands) - 1;
            if (band == kGroundGradientBands - 1 || y1 >= SCREEN_HEIGHT) {
                y1 = SCREEN_HEIGHT - 1;
            }

            const f32 t = static_cast<f32>(band + 1) / static_cast<f32>(kGroundGradientBands);
            const u8 bandAlpha = static_cast<u8>((groundAlpha * t * t) + 0.5f);
            if (bandAlpha == 0 || y1 < y0) {
                continue;
            }

            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, groundColor[0], groundColor[1], groundColor[2], bandAlpha);
            gDPFillRectangle(POLY_XLU_DISP++, 0, y0, SCREEN_WIDTH - 1, y1);
        }
    }

    CLOSE_DISPS(gfxCtx);
}

static void DrawGlobalIllumination() {
    PlayState* play = gPlayState;
    if (play == NULL || play->state.gfxCtx == NULL) {
        return;
    }

    // Do not tint over strong scripted full-screen fades/flashes such as game-over and transition effects.
    if (play->envCtx.fillScreen && play->envCtx.screenFillColor[3] > 96) {
        return;
    }

    const f32 intensity = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"), kDefaultIntensity);
    const f32 groundBounce =
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"), kDefaultGroundBounce);
    const f32 groundCoverage =
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundCoverage"), kDefaultGroundCoverage);

    // Keep the alpha deliberately modest. This lifts dark cel bands without flattening the direct-light contrast.
    const u8 skyAlpha = GiIntensityToAlpha(intensity, 0.50f);
    const u8 groundAlpha = GiIntensityToAlpha(groundBounce, 0.55f);
    if (skyAlpha == 0 && groundAlpha == 0) {
        return;
    }

    u8 skyColor[3];
    u8 groundColor[3];
    GiComputeIndirectColors(play, skyColor, groundColor);
    GiDrawComposite(play, skyColor, skyAlpha, groundColor, groundAlpha, groundCoverage);
}

static void RegisterGlobalIllumination() {
    const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"), 1) != 0;
    COND_HOOK(OnPlayDrawEnd, enabled, DrawGlobalIllumination);
}

static RegisterShipInitFunc sGlobalIlluminationInit(RegisterGlobalIllumination,
                                                    { CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled") });
