#!/usr/bin/env python3
from pathlib import Path

HEADER = Path("libultraship/include/ship/Context.h")
SOURCE = Path("libultraship/src/ship/Context.cpp")
MOBILE_HEADER_SOURCE = Path("patches/libultraship-android/MobileImpl.h")
MOBILE_SOURCE_SOURCE = Path("patches/libultraship-android/MobileImpl.cpp")
MOBILE_HEADER_TARGET = Path("libultraship/include/ship/port/mobile/MobileImpl.h")
MOBILE_SOURCE_TARGET = Path("libultraship/src/ship/port/mobile/MobileImpl.cpp")
RAPI_HEADER = Path("libultraship/include/fast/backends/gfx_rendering_api.h")
OGL_HEADER = Path("libultraship/include/fast/backends/gfx_opengl.h")
OGL_SOURCE = Path("libultraship/src/fast/backends/gfx_opengl.cpp")
OGL_FRAGMENT_SHADER = Path("libultraship/src/fast/shaders/opengl/default.shader.fs")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"Unable to patch {label}: expected source block was not found")
    return text.replace(old, new, 1)


def copy_android_mobile_layer() -> None:
    for source, target in (
        (MOBILE_HEADER_SOURCE, MOBILE_HEADER_TARGET),
        (MOBILE_SOURCE_SOURCE, MOBILE_SOURCE_TARGET),
    ):
        if not source.is_file():
            raise SystemExit(f"Android mobile source file is missing: {source}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")

    header = MOBILE_HEADER_TARGET.read_text(encoding="utf-8")
    source = MOBILE_SOURCE_TARGET.read_text(encoding="utf-8")
    required_header_markers = (
        "SetToggleButtonVisible",
        "SetFreeLookTouchEnabled",
        "SetFirstPersonAimingActive",
        "InjectMenuNavKeys",
        "ConsumeGamepadBackPress",
    )
    required_source_markers = (
        "Ship::Mobile::SetToggleButtonVisible",
        "Ship::Mobile::SetFreeLookTouchEnabled",
        "Ship::Mobile::SetFirstPersonAimingActive",
        "Java_com_dishii_soh_MainActivity_attachController",
        "Java_com_dishii_soh_MainActivity_setCameraState",
    )

    for marker in required_header_markers:
        if marker not in header:
            raise SystemExit(f"Android MobileImpl header validation failed: missing {marker}")
    for marker in required_source_markers:
        if marker not in source:
            raise SystemExit(f"Android MobileImpl source validation failed: missing {marker}")


def patch_screen_space_sun_shadows() -> None:
    rapi = RAPI_HEADER.read_text(encoding="utf-8")
    rapi = replace_once(
        rapi,
        """    // SOH [Enhancement] World light casting / actor shadows: the interpreter pushes the current stencil
""",
        """    // SOH [Enhancement] Lightweight Android direct shadows. The application projects the dominant
    // directional light into screen space once per frame. OpenGL ES samples the previous frame depth texture
    // while drawing the current frame, avoiding a second scene render.
    virtual void SetScreenSpaceSunShadow(const float screenDir[2], float depthStep, float strength, float length,
                                         float bias, bool enabled) {
        mSunShadowScreenDir[0] = screenDir[0];
        mSunShadowScreenDir[1] = screenDir[1];
        mSunShadowDepthStep = depthStep;
        mSunShadowStrength = strength;
        mSunShadowLength = length;
        mSunShadowBias = bias;
        mSunShadowEnabled = enabled;
    }

    // SOH [Enhancement] World light casting / actor shadows: the interpreter pushes the current stencil
""",
        "gfx_rendering_api.h screen-space shadow API",
    )
    rapi = replace_once(
        rapi,
        """  protected:
    float mToonLightDir[3] = { 0.0f, 0.0f, 1.0f };
""",
        """  protected:
    float mSunShadowScreenDir[2] = { 0.0f, 1.0f };
    float mSunShadowDepthStep = 0.0f;
    float mSunShadowStrength = 0.0f;
    float mSunShadowLength = 40.0f;
    float mSunShadowBias = 0.0018f;
    bool mSunShadowEnabled = false;
    float mToonLightDir[3] = { 0.0f, 0.0f, 1.0f };
""",
        "gfx_rendering_api.h screen-space shadow state",
    )
    RAPI_HEADER.write_text(rapi, encoding="utf-8")

    ogl_header = OGL_HEADER.read_text(encoding="utf-8")
    ogl_header = replace_once(
        ogl_header,
        """    GLint toon_debug_location;
};
""",
        """    GLint toon_debug_location;
    // SOH [Enhancement] Temporal screen-space sun shadows (OpenGL ES).
    GLint sun_shadow_depth_location;
    GLint sun_shadow_inv_size_location;
    GLint sun_shadow_direction_location;
    GLint sun_shadow_depth_step_location;
    GLint sun_shadow_strength_location;
    GLint sun_shadow_length_location;
    GLint sun_shadow_bias_location;
    GLint sun_shadow_enabled_location;
};
""",
        "gfx_opengl.h shadow uniform locations",
    )
    ogl_header = replace_once(
        ogl_header,
        """    void SetPerDrawUniforms();

    std::vector<TextureInfo> textures;
""",
        """    void SetPerDrawUniforms();
    void CaptureSunShadowDepth();

    std::vector<TextureInfo> textures;
""",
        "gfx_opengl.h capture declaration",
    )
    ogl_header = replace_once(
        ogl_header,
        """    size_t mPixelDepthRbSize = 0;
};
""",
        """    size_t mPixelDepthRbSize = 0;

    // Previous-frame depth used by the eight-step screen-space directional shadow trace.
    GLuint mSunShadowDepthTexture = 0;
    GLuint mSunShadowDepthFbo = 0;
    uint32_t mSunShadowDepthWidth = 0;
    uint32_t mSunShadowDepthHeight = 0;
    bool mSunShadowDepthValid = false;
};
""",
        "gfx_opengl.h temporal depth resources",
    )
    OGL_HEADER.write_text(ogl_header, encoding="utf-8")

    ogl_source = OGL_SOURCE.read_text(encoding="utf-8")
    old_per_draw = """void GfxRenderingAPIOGL::SetPerDrawUniforms() {
    if (mCurrentShaderProgram->usedTextures[0] || mCurrentShaderProgram->usedTextures[1]) {
        GLint filtering[2] = { textures[mCurrentTextureIds[0]].filtering, textures[mCurrentTextureIds[1]].filtering };
        glUniform1iv(mCurrentShaderProgram->texture_filtering_location, 2, filtering);

        GLint width[2] = { textures[mCurrentTextureIds[0]].width, textures[mCurrentTextureIds[1]].width };
        glUniform1iv(mCurrentShaderProgram->texture_width_location, 2, width);

        GLint height[2] = { textures[mCurrentTextureIds[0]].height, textures[mCurrentTextureIds[1]].height };
        glUniform1iv(mCurrentShaderProgram->texture_height_location, 2, height);
    }

    // SOH [Enhancement] Toon lighting: per-object dominant light + frame-global ramp shape, both
    // pushed in by the application (SetToonLighting / SetToonRamp). No config reads in the framework.
    if (mCurrentShaderProgram->opt_toon) {
        glUniform3fv(mCurrentShaderProgram->toon_light_dir_location, 1, mToonLightDir);
        glUniform3fv(mCurrentShaderProgram->toon_light_color_location, 1, mToonLightColor);
        glUniform3fv(mCurrentShaderProgram->toon_ambient_location, 1, mToonAmbient);
        glUniform1f(mCurrentShaderProgram->toon_ramp_center_location, mToonRampCenter);
        glUniform1f(mCurrentShaderProgram->toon_ramp_softness_location, mToonRampSoftness);
        glUniform1f(mCurrentShaderProgram->toon_highlight_intensity_location, mToonHighlightIntensity);
        glUniform1f(mCurrentShaderProgram->toon_shadow_intensity_location, mToonShadowIntensity);
        glUniform1f(mCurrentShaderProgram->toon_debug_location, mToonDebug);
    }
}
"""
    new_per_draw = """void GfxRenderingAPIOGL::SetPerDrawUniforms() {
    if (mCurrentShaderProgram->usedTextures[0] || mCurrentShaderProgram->usedTextures[1]) {
        GLint filtering[2] = { textures[mCurrentTextureIds[0]].filtering, textures[mCurrentTextureIds[1]].filtering };
        glUniform1iv(mCurrentShaderProgram->texture_filtering_location, 2, filtering);

        GLint width[2] = { textures[mCurrentTextureIds[0]].width, textures[mCurrentTextureIds[1]].width };
        glUniform1iv(mCurrentShaderProgram->texture_width_location, 2, width);

        GLint height[2] = { textures[mCurrentTextureIds[0]].height, textures[mCurrentTextureIds[1]].height };
        glUniform1iv(mCurrentShaderProgram->texture_height_location, 2, height);
    }

    // SOH [Enhancement] Toon lighting: per-object dominant light + frame-global ramp shape, both
    // pushed in by the application (SetToonLighting / SetToonRamp). No config reads in the framework.
    if (mCurrentShaderProgram->opt_toon) {
        glUniform3fv(mCurrentShaderProgram->toon_light_dir_location, 1, mToonLightDir);
        glUniform3fv(mCurrentShaderProgram->toon_light_color_location, 1, mToonLightColor);
        glUniform3fv(mCurrentShaderProgram->toon_ambient_location, 1, mToonAmbient);
        glUniform1f(mCurrentShaderProgram->toon_ramp_center_location, mToonRampCenter);
        glUniform1f(mCurrentShaderProgram->toon_ramp_softness_location, mToonRampSoftness);
        glUniform1f(mCurrentShaderProgram->toon_highlight_intensity_location, mToonHighlightIntensity);
        glUniform1f(mCurrentShaderProgram->toon_shadow_intensity_location, mToonShadowIntensity);
        glUniform1f(mCurrentShaderProgram->toon_debug_location, mToonDebug);
    }

#ifdef USE_OPENGLES
    // Only depth-tested world draws receive the effect; UI, menus and most overlays stay untouched.
    const bool shadowActive = mSunShadowEnabled && mSunShadowDepthValid && mCurrentDepthTest;
    const float invSize[2] = {
        mSunShadowDepthWidth > 0 ? 1.0f / (float)mSunShadowDepthWidth : 0.0f,
        mSunShadowDepthHeight > 0 ? 1.0f / (float)mSunShadowDepthHeight : 0.0f,
    };
    glUniform2fv(mCurrentShaderProgram->sun_shadow_inv_size_location, 1, invSize);
    glUniform2fv(mCurrentShaderProgram->sun_shadow_direction_location, 1, mSunShadowScreenDir);
    glUniform1f(mCurrentShaderProgram->sun_shadow_depth_step_location, mSunShadowDepthStep);
    glUniform1f(mCurrentShaderProgram->sun_shadow_strength_location, mSunShadowStrength);
    glUniform1f(mCurrentShaderProgram->sun_shadow_length_location, mSunShadowLength);
    glUniform1f(mCurrentShaderProgram->sun_shadow_bias_location, mSunShadowBias);
    glUniform1f(mCurrentShaderProgram->sun_shadow_enabled_location, shadowActive ? 1.0f : 0.0f);

    if (shadowActive) {
        const int restoreUnit = mLastActiveTexture >= 0 ? mLastActiveTexture : 0;
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, mSunShadowDepthTexture);
        glActiveTexture(GL_TEXTURE0 + restoreUnit);
    }
#endif
}
"""
    ogl_source = replace_once(ogl_source, old_per_draw, new_per_draw, "gfx_opengl.cpp per-draw shadow uniforms")

    ogl_source = replace_once(
        ogl_source,
        """    prg->toon_debug_location = glGetUniformLocation(shader_program, "toon_debug");

    LoadShader(prg);
""",
        """    prg->toon_debug_location = glGetUniformLocation(shader_program, "toon_debug");

    // SOH [Enhancement] Temporal screen-space sun shadow uniforms.
    prg->sun_shadow_depth_location = glGetUniformLocation(shader_program, "sun_shadow_depth");
    prg->sun_shadow_inv_size_location = glGetUniformLocation(shader_program, "sun_shadow_inv_size");
    prg->sun_shadow_direction_location = glGetUniformLocation(shader_program, "sun_shadow_direction");
    prg->sun_shadow_depth_step_location = glGetUniformLocation(shader_program, "sun_shadow_depth_step");
    prg->sun_shadow_strength_location = glGetUniformLocation(shader_program, "sun_shadow_strength");
    prg->sun_shadow_length_location = glGetUniformLocation(shader_program, "sun_shadow_length");
    prg->sun_shadow_bias_location = glGetUniformLocation(shader_program, "sun_shadow_bias");
    prg->sun_shadow_enabled_location = glGetUniformLocation(shader_program, "sun_shadow_enabled");

    LoadShader(prg);
""",
        "gfx_opengl.cpp shadow uniform lookup",
    )
    ogl_source = replace_once(
        ogl_source,
        """    if (cc_features.used_blend[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexBlend1");
        glUniform1i(sampler_location, 5);
    }

    return prg;
""",
        """    if (cc_features.used_blend[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTexBlend1");
        glUniform1i(sampler_location, 5);
    }
#ifdef USE_OPENGLES
    if (prg->sun_shadow_depth_location >= 0) {
        glUniform1i(prg->sun_shadow_depth_location, 7);
    }
#endif

    return prg;
""",
        "gfx_opengl.cpp depth sampler binding",
    )

    old_end_frame = """void GfxRenderingAPIOGL::EndFrame() {
    glFlush();
}
"""
    new_end_frame = """void GfxRenderingAPIOGL::CaptureSunShadowDepth() {
#ifdef USE_OPENGLES
    if (!mSunShadowEnabled) {
        mSunShadowDepthValid = false;
        return;
    }

    GLint viewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    uint32_t width = mFrameBuffers.empty() ? 0 : mFrameBuffers[mCurrentFrameBuffer].width;
    uint32_t height = mFrameBuffers.empty() ? 0 : mFrameBuffers[mCurrentFrameBuffer].height;
    if (width == 0 || height == 0) {
        width = (uint32_t)std::max(viewport[0] + viewport[2], 1);
        height = (uint32_t)std::max(viewport[1] + viewport[3], 1);
    }

    if (mSunShadowDepthTexture == 0) {
        glGenTextures(1, &mSunShadowDepthTexture);
    }
    if (mSunShadowDepthFbo == 0) {
        glGenFramebuffers(1, &mSunShadowDepthFbo);
    }

    GLint previousReadFbo = 0;
    GLint previousDrawFbo = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

    if (mSunShadowDepthWidth != width || mSunShadowDepthHeight != height) {
        glBindTexture(GL_TEXTURE_2D, mSunShadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL,
                     GL_UNSIGNED_INT_24_8, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, mSunShadowDepthFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               mSunShadowDepthTexture, 0);
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
        mSunShadowDepthValid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        mSunShadowDepthWidth = width;
        mSunShadowDepthHeight = height;
    }

    if (mSunShadowDepthValid) {
        const GLuint sourceFbo = mFrameBuffers.empty() ? 0 : mFrameBuffers[mCurrentFrameBuffer].fbo;
        while (glGetError() != GL_NO_ERROR) {
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mSunShadowDepthFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        mSunShadowDepthValid = glGetError() == GL_NO_ERROR;
    }

    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previousReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previousDrawFbo);
#endif
}

void GfxRenderingAPIOGL::EndFrame() {
    CaptureSunShadowDepth();
    glFlush();
}
"""
    ogl_source = replace_once(ogl_source, old_end_frame, new_end_frame, "gfx_opengl.cpp temporal depth capture")
    OGL_SOURCE.write_text(ogl_source, encoding="utf-8")

    shader = OGL_FRAGMENT_SHADER.read_text(encoding="utf-8")
    shader = replace_once(
        shader,
        """uniform int texture_filtering[2];

#define TEX_OFFSET(off) @{texture}(tex, texCoord - off / texSize)
""",
        """uniform int texture_filtering[2];

// SOH [Enhancement] Previous-frame depth directional shadow. OpenGL ES only; desktop variants compile
// the same Fast3D material without this mobile optimization.
@if(opengles)
uniform sampler2D sun_shadow_depth;
uniform vec2 sun_shadow_inv_size;
uniform vec2 sun_shadow_direction;
uniform float sun_shadow_depth_step;
uniform float sun_shadow_strength;
uniform float sun_shadow_length;
uniform float sun_shadow_bias;
uniform float sun_shadow_enabled;

float computeSunShadow() {
    if (sun_shadow_enabled < 0.5 || gl_FragCoord.z >= 0.9999) {
        return 0.0;
    }

    vec2 uv = gl_FragCoord.xy * sun_shadow_inv_size;
    float currentDepth = gl_FragCoord.z;
    float stridePixels = sun_shadow_length / 8.0;
    float occlusion = 0.0;

    // Eight taps are a deliberate mobile budget. The ray follows the projected dominant light and compares
    // against the previous frame depth, giving direct contact/directional shadows without a second scene pass.
    for (int i = 1; i <= 8; ++i) {
        float stepIndex = float(i);
        vec2 sampleUv = uv + sun_shadow_direction * sun_shadow_inv_size * (stridePixels * stepIndex);
        if (sampleUv.x <= 0.001 || sampleUv.x >= 0.999 || sampleUv.y <= 0.001 || sampleUv.y >= 0.999) {
            continue;
        }

        float sampledDepth = texture(sun_shadow_depth, sampleUv).r;
        float expectedDepth = currentDepth + (sun_shadow_depth_step * stepIndex);
        float localBias = sun_shadow_bias + (stepIndex * 0.00012);
        bool inFront = sampledDepth < expectedDepth - localBias;
        bool closeEnough = sampledDepth > currentDepth - 0.22;
        if (inFront && closeEnough) {
            occlusion = max(occlusion, 1.0 - (stepIndex / 12.0));
        }
    }

    return clamp(occlusion * sun_shadow_strength, 0.0, 1.0);
}
@end

#define TEX_OFFSET(off) @{texture}(tex, texCoord - off / texSize)
""",
        "default.shader.fs screen-space shadow uniforms",
    )
    shader = replace_once(
        shader,
        """    // TODO discard if alpha is 0?

    // SOH [Enhancement] Toon lighting: re-light the (white-shaded) albedo with the single
""",
        """    // TODO discard if alpha is 0?

    @if(opengles)
        float sunShadow = computeSunShadow();
    @else
        float sunShadow = 0.0;
    @end

    // SOH [Enhancement] Toon lighting: re-light the (white-shaded) albedo with the single
""",
        "default.shader.fs shadow evaluation",
    )
    shader = replace_once(
        shader,
        """        float toonRamp = smoothstep(toon_ramp_center - toon_ramp_softness,
                                    toon_ramp_center + toon_ramp_softness, toonNL);
""",
        """        float toonRamp = smoothstep(toon_ramp_center - toon_ramp_softness,
                                    toon_ramp_center + toon_ramp_softness, toonNL);
        // Occlusion removes only the direct toon band; ambient/shadow colour remains intact.
        toonRamp *= 1.0 - sunShadow;
""",
        "default.shader.fs toon direct-light shadow",
    )
    shader = replace_once(
        shader,
        """    @end

    @if(o_fog)
""",
        """    @end

    @if(opengles)
        @if(!o_toon)
            // Ordinary map materials do not expose direct and ambient terms separately. A restrained multiply
            // approximates removal of direct light while preserving texture colour and avoiding black blocks.
            texel.rgb *= 1.0 - (sunShadow * 0.58);
        @end
    @end

    @if(o_fog)
""",
        "default.shader.fs ordinary material shadow",
    )
    OGL_FRAGMENT_SHADER.write_text(shader, encoding="utf-8")

    validations = {
        RAPI_HEADER: ("SetScreenSpaceSunShadow", "mSunShadowStrength"),
        OGL_HEADER: ("sun_shadow_depth_location", "mSunShadowDepthTexture"),
        OGL_SOURCE: ("CaptureSunShadowDepth", "GL_TEXTURE7", "glBlitFramebuffer"),
        OGL_FRAGMENT_SHADER: ("computeSunShadow", "for (int i = 1; i <= 8; ++i)", "toonRamp *= 1.0 - sunShadow"),
    }
    for path, markers in validations.items():
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                raise SystemExit(f"Screen-space sun shadow patch failed for {path}: missing {marker}")


header = HEADER.read_text(encoding="utf-8")
header = replace_once(
    header,
    '    static std::string LocateFileAcrossAppDirs(const std::string path, std::string appName = "");\n',
    '    static std::string LocateFileAcrossAppDirs(const std::string path, std::string appName = "");\n'
    '#if defined(__ANDROID__)\n'
    '    static void SetAndroidDataRootPath(const std::string& path);\n'
    '#endif\n',
    "Context.h declaration",
)
HEADER.write_text(header, encoding="utf-8")

source = SOURCE.read_text(encoding="utf-8")
source = replace_once(
    source,
    'std::weak_ptr<Context> Context::mContext;\n',
    'std::weak_ptr<Context> Context::mContext;\n\n'
    '#if defined(__ANDROID__)\n'
    'static std::string sAndroidDataRootPath = "/storage/emulated/0/SOHCS-GI";\n\n'
    'static const std::string& GetAndroidDataRootPath() {\n'
    '    return sAndroidDataRootPath;\n'
    '}\n'
    '#endif\n',
    "Context.cpp Android storage state",
)
source = replace_once(
    source,
    'std::string Context::GetShortName() {\n'
    '    return mShortName;\n'
    '}\n',
    'std::string Context::GetShortName() {\n'
    '    return mShortName;\n'
    '}\n\n'
    '#if defined(__ANDROID__)\n'
    'void Context::SetAndroidDataRootPath(const std::string& path) {\n'
    '    if (!path.empty()) {\n'
    '        sAndroidDataRootPath = path;\n'
    '    }\n'
    '}\n'
    '#endif\n',
    "Context.cpp setter",
)

legacy_android_block = (
    '#if defined(__ANDROID__)\n'
    '    const char* externaldir = SDL_AndroidGetExternalStoragePath();\n'
    '    if (externaldir != NULL) {\n'
    '        return externaldir;\n'
    '    }\n'
    '#endif\n'
)
new_android_block = (
    '#if defined(__ANDROID__)\n'
    '    return GetAndroidDataRootPath();\n'
    '#endif\n'
)

if source.count(legacy_android_block) not in (0, 2):
    raise SystemExit(
        "Unable to patch Context.cpp Android path functions: expected zero or two legacy blocks"
    )
source = source.replace(legacy_android_block, new_android_block)

required_markers = (
    "sAndroidDataRootPath",
    "Context::SetAndroidDataRootPath",
    "return GetAndroidDataRootPath();",
)
for marker in required_markers:
    if marker not in source:
        raise SystemExit(f"Android data-root patch validation failed: missing {marker}")

SOURCE.write_text(source, encoding="utf-8")
copy_android_mobile_layer()
patch_screen_space_sun_shadows()
print("Roborich libultraship Android storage, mobile and screen-space sun shadow integrations applied successfully.")
