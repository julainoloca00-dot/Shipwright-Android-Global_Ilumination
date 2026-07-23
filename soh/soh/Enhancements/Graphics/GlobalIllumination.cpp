// Lightweight directional sun shadows for Android/OpenGL ES.
//
// This is not global illumination and does not add ambient light or ground bounce. The game sends the
// dominant directional light projected into screen space to libultraship. The OpenGL ES backend then uses
// the previous frame's depth buffer to trace a short ray toward that light in the fragment shader. Opaque
// map geometry and actors can therefore block the direct light without rendering the scene a second time.

#include <libultraship/bridge.h>
#include <ship/Context.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

#include <cmath>
#include <memory>

extern "C" {
#include "z64.h"
#include "variables.h"

extern PlayState* gPlayState;
}

static constexpr f32 kDefaultStrength = 0.70f;
static constexpr f32 kDefaultLength = 40.0f;
static constexpr f32 kDefaultBias = 0.0018f;

static Fast::GfxRenderingAPI* GetRenderingApi() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (window == nullptr) {
        return nullptr;
    }

    auto interpreter = window->GetInterpreterWeak().lock();
    if (interpreter == nullptr) {
        return nullptr;
    }

    return interpreter->GetCurrentRenderingAPI();
}

static f32 ClampFloat(f32 value, f32 minimum, f32 maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static f32 VectorLength(f32 x, f32 y, f32 z) {
    return sqrtf((x * x) + (y * y) + (z * z));
}

static void NormalizeVector(f32* x, f32* y, f32* z) {
    const f32 length = VectorLength(*x, *y, *z);
    if (length > 0.0001f) {
        *x /= length;
        *y /= length;
        *z /= length;
    }
}

static s32 DirectionalLuminance(const LightInfo* light) {
    return static_cast<s32>(light->params.dir.color[0]) + static_cast<s32>(light->params.dir.color[1]) +
           static_cast<s32>(light->params.dir.color[2]);
}

static void DisableScreenSpaceSunShadow(Fast::GfxRenderingAPI* renderingApi) {
    const f32 disabledDirection[2] = { 0.0f, 0.0f };
    renderingApi->SetScreenSpaceSunShadow(disabledDirection, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

static void UpdateScreenSpaceSunShadow() {
    Fast::GfxRenderingAPI* renderingApi = GetRenderingApi();
    if (renderingApi == nullptr) {
        return;
    }

    const bool enabled =
        CVarGetInteger(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Enabled"), 1) != 0;
    PlayState* play = gPlayState;
    if (!enabled || play == nullptr) {
        DisableScreenSpaceSunShadow(renderingApi);
        return;
    }

    const LightInfo* light1 = &play->envCtx.dirLight1;
    const LightInfo* light2 = &play->envCtx.dirLight2;
    const LightInfo* dominant = DirectionalLuminance(light2) > DirectionalLuminance(light1) ? light2 : light1;

    f32 lightX = dominant->params.dir.x;
    f32 lightY = dominant->params.dir.y;
    f32 lightZ = dominant->params.dir.z;
    NormalizeVector(&lightX, &lightY, &lightZ);

    // View exposes the exact eye/look-at pair used to render this frame. Using it keeps the projected
    // light direction correct during normal gameplay, cutscenes, first-person aiming and sub-camera shots.
    f32 forwardX = play->view.lookAt.x - play->view.eye.x;
    f32 forwardY = play->view.lookAt.y - play->view.eye.y;
    f32 forwardZ = play->view.lookAt.z - play->view.eye.z;
    NormalizeVector(&forwardX, &forwardY, &forwardZ);

    if (VectorLength(forwardX, forwardY, forwardZ) < 0.0001f) {
        DisableScreenSpaceSunShadow(renderingApi);
        return;
    }

    // right = normalize(cross(forward, worldUp)).
    f32 rightX = -forwardZ;
    f32 rightY = 0.0f;
    f32 rightZ = forwardX;
    NormalizeVector(&rightX, &rightY, &rightZ);
    if (VectorLength(rightX, rightY, rightZ) < 0.0001f) {
        rightX = 1.0f;
        rightY = 0.0f;
        rightZ = 0.0f;
    }

    // cameraUp = cross(right, forward).
    f32 upX = (rightY * forwardZ) - (rightZ * forwardY);
    f32 upY = (rightZ * forwardX) - (rightX * forwardZ);
    f32 upZ = (rightX * forwardY) - (rightY * forwardX);
    NormalizeVector(&upX, &upY, &upZ);

    f32 screenX = (lightX * rightX) + (lightY * rightY) + (lightZ * rightZ);
    f32 screenY = (lightX * upX) + (lightY * upY) + (lightZ * upZ);
    const f32 screenLength = sqrtf((screenX * screenX) + (screenY * screenY));
    if (screenLength > 0.0001f) {
        screenX /= screenLength;
        screenY /= screenLength;
    } else {
        // Looking almost directly toward the light: use a stable vertical trace instead of producing NaNs.
        screenX = 0.0f;
        screenY = 1.0f;
    }

    const f32 lightAlongView =
        (lightX * forwardX) + (lightY * forwardY) + (lightZ * forwardZ);
    const f32 depthStep = lightAlongView * 0.0015f;
    const f32 strength = ClampFloat(
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Strength"), kDefaultStrength), 0.0f, 1.0f);
    const f32 length = ClampFloat(
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Length"), kDefaultLength), 4.0f, 96.0f);
    const f32 bias = ClampFloat(
        CVarGetFloat(CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Bias"), kDefaultBias), 0.0002f, 0.01f);

    const f32 screenDirection[2] = { screenX, screenY };
    renderingApi->SetScreenSpaceSunShadow(screenDirection, depthStep, strength, length, bias, true);
}

static void RegisterScreenSpaceSunShadows() {
    COND_HOOK(OnGameFrameUpdate, true, UpdateScreenSpaceSunShadow);
}

static RegisterShipInitFunc sScreenSpaceSunShadowInit(
    RegisterScreenSpaceSunShadows,
    { CVAR_ENHANCEMENT("Graphics.ScreenSpaceSunShadows.Enabled") });
