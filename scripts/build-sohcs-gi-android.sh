#!/usr/bin/env bash
set -euxo pipefail

ROBORICH_SHIPWRIGHT_SHA="85ab8273114fc57b40f7a0641d77692311c9625b"
ROBORICH_LUS_SHA="4698a3d6310d90900cec81e7e93a8337f4e6938f"
TARGET_BRANCH="android-sohcs-global-illumination"
PACKAGE_ID="com.linkzenic.sohcs.gi"
APP_LABEL="SOHCS GI"
DATA_FOLDER="SOHCS-GI"

cd "${GITHUB_WORKSPACE:-$(pwd)}"
git config --global --add safe.directory "$(pwd)"
git config user.name "SOHCS Android Builder"
git config user.email "actions@github.com"

git remote remove roborich 2>/dev/null || true
git remote add roborich https://github.com/roborich/Shipwright.git
git fetch --no-tags roborich wind-waker-style-cel-shading
git branch -f roborich-celshade FETCH_HEAD
test "$(git rev-parse roborich-celshade)" = "${ROBORICH_SHIPWRIGHT_SHA}"

if ! git merge-base --is-ancestor roborich-celshade HEAD; then
    set +e
    git merge --no-commit --no-ff roborich-celshade
    merge_exit=$?
    set -e

    if [ "${merge_exit}" -ne 0 ]; then
        git rm -f .github/workflows/generate-builds.yml 2>/dev/null || true

        git checkout --ours .gitignore
        if ! grep -q 'wind-waker-style-docs' .gitignore; then
            cat >> .gitignore <<'EOF'

# Allow images embedded in the Wind Waker-style design docs.
!wind-waker-style-docs/**/*.png
!wind-waker-style-docs/**/*.jpg

# Third-party/reference assets that are not part of the build.
/third-party/
/wind-waker-unpacked/
/ww-cloud-assets/
EOF
        fi

        cat > .gitmodules <<'EOF'
[submodule "libultraship"]
	path = libultraship
	url = https://github.com/roborich/libultraship.git
	branch = wind-waker-style-cel-shading
[submodule "ZAPDTR"]
	path = ZAPDTR
	url = https://github.com/please-be-nice/ZAPDTR
[submodule "OTRExporter"]
	path = OTRExporter
	url = https://github.com/please-be-nice/OTRExporter
EOF
        git add .gitignore .gitmodules

        git checkout --ours soh/soh/Enhancements/ArrowCycle.cpp
        git checkout --ours soh/soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h
        git checkout --ours soh/soh/OTRGlobals.cpp
        git add soh/soh/Enhancements/ArrowCycle.cpp
        git add soh/soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h
        git add soh/soh/OTRGlobals.cpp

        git update-index --force-remove libultraship || true
        git update-index --add --cacheinfo "160000,${ROBORICH_LUS_SHA},libultraship"

        unmerged="$(git diff --name-only --diff-filter=U)"
        if [ -n "${unmerged}" ]; then
            echo "Unresolved merge conflicts:"
            printf '%s\n' "${unmerged}"
            exit 1
        fi
    fi
fi

python3 - <<'PY'
from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if old not in text and new not in text:
        raise SystemExit(f"Expected text not found in {path}: {old}")
    p.write_text(text.replace(old, new), encoding="utf-8")


replace(
    "Android/app/build.gradle",
    'def androidAppVersionName = "9.2.3-android.5"',
    'def androidAppVersionName = "9.2.3-sohcs-gi.4"',
)
replace("Android/app/build.gradle", "versionCode 13", "versionCode 16")
replace(
    "Android/app/build.gradle",
    'applicationId "com.linkzenic.soh"',
    'applicationId "com.linkzenic.sohcs.gi"',
)
replace(
    "Android/app/build.gradle",
    'def newApkName = "soh_${buildType}_${gitTag}.apk"',
    'def newApkName = "sohcs_gi_${buildType}_${gitTag}.apk"',
)
replace(
    "Android/app/src/main/res/values/strings.xml",
    '<string name="app_name">Ship of Harkinian</string>',
    '<string name="app_name">SOHCS GI</string>',
)
replace(
    "soh/soh/OTRGlobals.h",
    'const std::string appShortName = "soh";',
    'const std::string appShortName = "sohcsgi";',
)

main = Path("Android/app/src/main/java/com/dishii/soh/MainActivity.java")
text = main.read_text(encoding="utf-8")
for old, new in {
    "/storage/emulated/0/SOH": "/storage/emulated/0/SOHCS-GI",
    '"com.dishii.soh.prefs"': '"com.linkzenic.sohcs.gi.prefs"',
    '"soh-android-support-1"': '"sohcs-gi-android-support-1"',
    '"SOH"': '"SOHCS-GI"',
}.items():
    text = text.replace(old, new)
main.write_text(text, encoding="utf-8")
PY

# Game-side proof: direct shadows only, no GI, bounce, stencil boxes or ambient modification.
test -f soh/soh/Enhancements/Graphics/ToonLighting.cpp
test -f soh/soh/SohGui/SohMenuWindWakerStyle.cpp
grep -q 'Graphics.ToonLighting.Enabled' soh/soh/Enhancements/Graphics/ToonLighting.cpp
test -f soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'Graphics.ScreenSpaceSunShadows.Enabled' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'SetScreenSpaceSunShadow' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'depthStep' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
if grep -Eq 'GlobalIllumination.GroundBounce|Lights_DirectionalSetInfo|lightCtx.ambientColor|gSPStencil|GI_STENCIL' \
    soh/soh/Enhancements/Graphics/GlobalIllumination.cpp; then
    echo "ERROR: obsolete GI/bounce/stencil implementation is still present"
    exit 1
fi

git add -A
if ! git diff --cached --quiet; then
    git commit -m "Merge real Roborich celshade with Android screen-space sun shadows v4"
fi

git submodule sync --recursive
git submodule update --init --recursive
test "$(git -C libultraship rev-parse HEAD)" = "${ROBORICH_LUS_SHA}"

# Apply Android storage/mobile integration and the OpenGL ES temporal-depth shadow backend.
python3 scripts/patch-roborich-lus-android-data-root.py
grep -q 'SetScreenSpaceSunShadow' libultraship/include/fast/backends/gfx_rendering_api.h
grep -q 'CaptureSunShadowDepth' libultraship/src/fast/backends/gfx_opengl.cpp
grep -q 'glBlitFramebuffer' libultraship/src/fast/backends/gfx_opengl.cpp
grep -q 'computeSunShadow' libultraship/src/fast/shaders/opengl/default.shader.fs
grep -q 'for (int i = 1; i <= 8; ++i)' libultraship/src/fast/shaders/opengl/default.shader.fs
grep -q 'toonRamp \*= 1.0 - sunShadow' libultraship/src/fast/shaders/opengl/default.shader.fs

printf 'sdk.dir=%s\n' "${ANDROID_SDK_ROOT}" > Android/local.properties
printf 'ndk.dir=%s\n' "${ANDROID_NDK_HOME}" >> Android/local.properties

apt-get update
apt-get install -y $(cat .github/workflows/apt-deps.txt) unzip binutils

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target GenerateSohOtr --parallel 3
test -f soh/soh.o2r
mkdir -p Android/app/src/main/assets
cp soh/soh.o2r Android/app/src/main/assets/soh.o2r

set -o pipefail
(
    cd Android
    GIT_TAG=sohcs-sun-shadow-v4-test ./gradlew assembleDebug --no-daemon --stacktrace
) 2>&1 | tee sohcs-gi-gradle.log

apk="$(find Android/app/build/outputs/apk/debug -type f -name '*.apk' -print -quit)"
test -n "${apk}"

mkdir -p artifacts
cp "${apk}" artifacts/SOHCS-Sun-Shadows-v4-debug.apk
sha256sum artifacts/SOHCS-Sun-Shadows-v4-debug.apk > artifacts/SOHCS-Sun-Shadows-v4-debug.sha256

aapt="$(find "${ANDROID_SDK_ROOT}/build-tools" -type f -name aapt | sort -V | tail -1)"
"${aapt}" dump badging "${apk}" | tee sohcs-gi-badging.txt
grep -q "package: name='${PACKAGE_ID}'" sohcs-gi-badging.txt
grep -q "application-label:'${APP_LABEL}'" sohcs-gi-badging.txt
grep -q "versionCode='16'" sohcs-gi-badging.txt
grep -q "versionName='9.2.3-sohcs-gi.4'" sohcs-gi-badging.txt

toon_object="$(find Android/app/.cxx -type f -name 'ToonLighting.cpp.o' -print -quit)"
shadow_object="$(find Android/app/.cxx -type f -name 'GlobalIllumination.cpp.o' -print -quit)"
ogl_object="$(find Android/app/.cxx -type f -name 'gfx_opengl.cpp.o' -print -quit)"
test -s "${toon_object}"
test -s "${shadow_object}"
test -s "${ogl_object}"

rm -rf verify-apk
mkdir -p verify-apk
unzip -q "${apk}" 'lib/arm64-v8a/libsoh.so' -d verify-apk
strings verify-apk/lib/arm64-v8a/libsoh.so > sohcs-gi-native-strings.txt

{
    echo "Native renderer validation"
    echo "=========================="
    echo "Toon object: ${toon_object}"
    echo "Sun shadow game object: ${shadow_object}"
    echo "OpenGL ES backend object: ${ogl_object}"
    echo
    for marker in \
        'Graphics.ToonLighting.Enabled' \
        'Graphics.ScreenSpaceSunShadows.Enabled' \
        'Graphics.ScreenSpaceSunShadows.Strength' \
        'Graphics.ScreenSpaceSunShadows.Length' \
        'Graphics.ScreenSpaceSunShadows.Bias'; do
        grep -a -Fq "${marker}" verify-apk/lib/arm64-v8a/libsoh.so
        echo "PRESENT in packaged libsoh.so: ${marker}"
    done
} | tee artifacts/NATIVE-VALIDATION.txt

cat > artifacts/BUILD-INFO.txt <<EOF
Android branch: ${TARGET_BRANCH}
Roborich celshade: $(git rev-parse roborich-celshade)
Build commit: $(git rev-parse HEAD)
libultraship base: $(git -C libultraship rev-parse HEAD)
Package: ${PACKAGE_ID}
Application: ${APP_LABEL}
Version: 9.2.3-sohcs-gi.4 (16)
Data folder: ${DATA_FOLDER}
Direct shadow implementation:
- Previous-frame depth texture captured with glBlitFramebuffer
- Eight-step screen-space ray toward the dominant sun/moon light
- Toon materials remove only the direct lit band when occluded
- Ordinary depth-tested map materials receive a restrained direct-shadow multiplier
- HUD and non-depth-tested overlays are excluded
- No global illumination, ambient lift, ground bounce, stencil GI boxes or second scene render
Known limitation:
- Screen-space only: off-camera occluders cannot cast into the visible image
EOF

echo "SOHCS screen-space sun shadow v4 APK built and validated successfully."
