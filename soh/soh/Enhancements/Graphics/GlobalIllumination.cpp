// Lightweight scene-wide indirect lighting for the Wind Waker-style renderer.
//
// This pass runs once after the complete 3D scene (map and actors) and before the HUD. Two large stencil
// volumes select the already-rendered geometry using the scene depth buffer: one provides sky/environment
// indirect light and the other provides warm ground bounce. It does not blur or soften cel-shaded edges.

#include <libultraship/bridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/frame_interpolation.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

extern PlayState* gPlayState;
}

// Must match Fast::StencilMode in libultraship. These are the same modes already used by WorldLighting.cpp.
static constexpr s32 GI_STENCIL_OFF = 0;
static constexpr s32 GI_STENCIL_VOLUME_INCR = 1;
static constexpr s32 GI_STENCIL_VOLUME_DECR = 2;
static constexpr s32 GI_STENCIL_COMPOSITE = 3;

static constexpr f32 kDefaultIntensity = 0.22f;
static constexpr f32 kDefaultGroundBounce = 0.14f;
static constexpr f32 kDefaultBounceHeight = 300.0f;
static constexpr f32 kGlobalHalfExtent = 18000.0f;
static constexpr f32 kGlobalDepth = 30000.0f;

static Vtx sGiBoxVtx[8];
static bool sGiBoxBuilt = false;

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
    value = GiClampF(value, 0.0f, 1.0f);
    return static_cast<u8>((value * 255.0f) + 0.5f);
}

static u8 GiIntensityToAlpha(f32 intensity) {
    return GiToByte(GiClampF(intensity, 0.0f, 1.0f) * 0.5f);
}

static void GiSetBoxVertex(s32 index, s16 x, s16 y, s16 z) {
    sGiBoxVtx[index].v.ob[0] = x;
    sGiBoxVtx[index].v.ob[1] = y;
    sGiBoxVtx[index].v.ob[2] = z;
    sGiBoxVtx[index].v.flag = 0;
    sGiBoxVtx[index].v.tc[0] = 0;
    sGiBoxVtx[index].v.tc[1] = 0;
    sGiBoxVtx[index].v.cn[0] = 255;
    sGiBoxVtx[index].v.cn[1] = 255;
    sGiBoxVtx[index].v.cn[2] = 255;
    sGiBoxVtx[index].v.cn[3] = 255;
}

static void GiBuildBox() {
    if (sGiBoxBuilt) {
        return;
    }

    GiSetBoxVertex(0, -100, -100, -100);
    GiSetBoxVertex(1, 100, -100, -100);
    GiSetBoxVertex(2, 100, 100, -100);
    GiSetBoxVertex(3, -100, 100, -100);
    GiSetBoxVertex(4, -100, -100, 100);
    GiSetBoxVertex(5, 100, -100, 100);
    GiSetBoxVertex(6, 100, 100, 100);
    GiSetBoxVertex(7, -100, 100, 100);

    sGiBoxBuilt = true;
}

static void GiEmitBox(GraphicsContext* gfxCtx) {
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_OPA_DISP++, (uintptr_t)sGiBoxVtx, 8, 0);
    gSP2Triangles(POLY_OPA_DISP++, 4, 5, 6, 0, 4, 6, 7, 0);
    gSP2Triangles(POLY_OPA_DISP++, 1, 0, 3, 0, 1, 3, 2, 0);
    gSP2Triangles(POLY_OPA_DISP++, 0, 4, 7, 0, 0, 7, 3, 0);
    gSP2Triangles(POLY_OPA_DISP++, 5, 1, 2, 0, 5, 2, 6, 0);
    gSP2Triangles(POLY_OPA_DISP++, 3, 7, 6, 0, 3, 6, 2, 0);
    gSP2Triangles(POLY_OPA_DISP++, 0, 1, 5, 0, 0, 5, 4, 0);
    CLOSE_DISPS(gfxCtx);
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
    const f32 neutralLift = 0.025f + (0.08f * daylight);
    static constexpr f32 warmGround[3] = { 1.0f, 0.82f, 0.62f };

    for (s32 i = 0; i < 3; ++i) {
        const f32 ambient = play->lightCtx.ambientColor[i] / 255.0f;
        const f32 fog = play->lightCtx.fogColor[i] / 255.0f;
        const f32 key = dominant->params.dir.color[i] / 255.0f;

        const f32 sky = neutralLift + (ambient * 0.55f) + (key * 0.28f) + (fog * 0.10f);
        const f32 ground =
            (neutralLift * 0.65f) + (ambient * 0.45f) + (fog * 0.18f) + (key * warmGround[i] * 0.18f);

        skyColor[i] = GiToByte(sky);
        groundColor[i] = GiToByte(ground);
    }
}

static void GiLoadBoxMatrix(PlayState* play, f32 centerX, f32 centerY, f32 centerZ, f32 halfX, f32 halfY,
                            f32 halfZ) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(centerX, centerY, centerZ, MTXMODE_NEW);
    Matrix_Scale(halfX * 0.01f, halfY * 0.01f, halfZ * 0.01f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Z-fail stencil mask pass. It reads the depth produced by both map and actors but writes no colour or depth.
static void GiMaskPass(PlayState* play, s32 stencilMode, u32 cullMode) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, stencilMode);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_FRONT | G_CULL_BACK);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | cullMode);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                      PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 0, 0, 0, 0);
    CLOSE_DISPS(play->state.gfxCtx);
    GiEmitBox(play->state.gfxCtx);
}

// The stencil mask selects the visible 3D surfaces inside the volume. Because this runs after actor drawing,
// the composite reaches opaque map geometry and opaque actors together without touching the HUD.
static void GiCompositePass(PlayState* play, const u8 color[3], u8 alpha) {
    if (alpha == 0) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, GI_STENCIL_COMPOSITE);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_ZBUFFER | G_CULL_BACK);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_FRONT);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE, 0, 0, 0,
                      PRIMITIVE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, color[0], color[1], color[2], alpha);
    CLOSE_DISPS(play->state.gfxCtx);
    GiEmitBox(play->state.gfxCtx);
}

static void GiDrawIndirectVolume(PlayState* play, f32 centerX, f32 centerY, f32 centerZ, f32 halfX, f32 halfY,
                                 f32 halfZ, const u8 color[3], u8 alpha) {
    if (alpha == 0 || halfX <= 0.0f || halfY <= 0.0f || halfZ <= 0.0f) {
        return;
    }

    GiLoadBoxMatrix(play, centerX, centerY, centerZ, halfX, halfY, halfZ);
    GiMaskPass(play, GI_STENCIL_VOLUME_INCR, G_CULL_FRONT);
    GiMaskPass(play, GI_STENCIL_VOLUME_DECR, G_CULL_BACK);
    GiCompositePass(play, color, alpha);
}

static void DrawGlobalIllumination() {
    PlayState* play = gPlayState;
    if (play == NULL || play->state.gfxCtx == NULL) {
        return;
    }

    GiBuildBox();

    const f32 intensity = CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Intensity"), kDefaultIntensity);
    const f32 groundBounce =
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.GroundBounce"), kDefaultGroundBounce);
    const f32 bounceHeight =
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.BounceHeight"), kDefaultBounceHeight);

    const u8 skyAlpha = GiIntensityToAlpha(intensity);
    const u8 groundAlpha = GiIntensityToAlpha(groundBounce);
    if (skyAlpha == 0 && groundAlpha == 0) {
        return;
    }

    u8 skyColor[3];
    u8 groundColor[3];
    GiComputeIndirectColors(play, skyColor, groundColor);

    const f32 eyeX = play->view.eye.x;
    const f32 eyeY = play->view.eye.y;
    const f32 eyeZ = play->view.eye.z;

    // The sky/environment volume surrounds the entire visible scene, affecting map and actors equally.
    GiDrawIndirectVolume(play, eyeX, eyeY, eyeZ, kGlobalHalfExtent, kGlobalHalfExtent, kGlobalHalfExtent, skyColor,
                         skyAlpha);

    if (groundAlpha > 0) {
        f32 floorY = eyeY - 50.0f;
        Player* player = GET_PLAYER(play);
        if (player != NULL) {
            if (player->actor.floorHeight > -30000.0f && player->actor.floorHeight < 30000.0f) {
                floorY = player->actor.floorHeight;
            } else {
                floorY = player->actor.world.pos.y;
            }
        }

        const f32 top = floorY + GiClampF(bounceHeight, 0.0f, 1200.0f);
        const f32 bottom = top - kGlobalDepth;
        const f32 centerY = (top + bottom) * 0.5f;
        const f32 halfY = (top - bottom) * 0.5f;

        GiDrawIndirectVolume(play, eyeX, centerY, eyeZ, kGlobalHalfExtent, halfY, kGlobalHalfExtent, groundColor,
                             groundAlpha);
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPStencil(POLY_OPA_DISP++, GI_STENCIL_OFF);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterGlobalIllumination() {
    const bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled"), 1) != 0;
    COND_HOOK(OnPlayDrawEnd, enabled, DrawGlobalIllumination);
}

static RegisterShipInitFunc sGlobalIlluminationInit(RegisterGlobalIllumination,
                                                    { CVAR_ENHANCEMENT("Graphics.GlobalIllumination.Enabled") });
