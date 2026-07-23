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
        # Desktop CI was intentionally removed by the Android port.
        git rm -f .github/workflows/generate-builds.yml 2>/dev/null || true

        # Android ignore rules plus the generated/reference exclusions used by the celshade fork.
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

        # Roborich renderer plus the Android-compatible extraction tools.
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

        # These Android files are supersets of the generic changes on the celshade side.
        git checkout --ours soh/soh/Enhancements/ArrowCycle.cpp
        git checkout --ours soh/soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h
        git checkout --ours soh/soh/OTRGlobals.cpp
        git add soh/soh/Enhancements/ArrowCycle.cpp
        git add soh/soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h
        git add soh/soh/OTRGlobals.cpp

        # Resolve the gitlink to the exact renderer revision referenced by Roborich's celshade head.
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
    'def androidAppVersionName = "9.2.3-sohcs-gi.2"',
)
replace(
    "Android/app/build.gradle",
    "versionCode 13",
    "versionCode 14",
)
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

# Source-level proof that this is the real celshade fork plus scene-wide stencil GI.
test -f soh/soh/Enhancements/Graphics/ToonLighting.cpp
test -f soh/soh/SohGui/SohMenuWindWakerStyle.cpp
grep -q 'Graphics.ToonLighting.Enabled' soh/soh/Enhancements/Graphics/ToonLighting.cpp
test -f soh/soh/Enhancements/Graphics/WorldLighting.cpp
grep -q 'gSPStencil' soh/soh/Enhancements/Graphics/WorldLighting.cpp
test -f soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'Graphics.GlobalIllumination.Enabled' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'Graphics.GlobalIllumination.BounceHeight' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'gSPStencil' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp
grep -q 'GI_STENCIL_COMPOSITE' soh/soh/Enhancements/Graphics/GlobalIllumination.cpp

git add -A
if ! git diff --cached --quiet; then
    git commit -m "Merge real Roborich celshade with scene-wide Android GI v2"
fi

# Initialize the selected renderer and Android extractor forks.
git submodule sync --recursive
git submodule update --init --recursive
test "$(git -C libultraship rev-parse HEAD)" = "${ROBORICH_LUS_SHA}"

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
    GIT_TAG=sohcs-gi-v2-test ./gradlew assembleDebug --no-daemon --stacktrace
) 2>&1 | tee sohcs-gi-gradle.log

apk="$(find Android/app/build/outputs/apk/debug -type f -name '*.apk' -print -quit)"
test -n "${apk}"

# Copy the APK immediately. A later diagnostic must never hide a successfully built application.
mkdir -p artifacts
cp "${apk}" artifacts/SOHCS-GI-debug.apk
sha256sum artifacts/SOHCS-GI-debug.apk > artifacts/SOHCS-GI-debug.sha256

aapt="$(find "${ANDROID_SDK_ROOT}/build-tools" -type f -name aapt | sort -V | tail -1)"
"${aapt}" dump badging "${apk}" | tee sohcs-gi-badging.txt
grep -q "package: name='${PACKAGE_ID}'" sohcs-gi-badging.txt
grep -q "application-label:'${APP_LABEL}'" sohcs-gi-badging.txt
grep -q "versionCode='14'" sohcs-gi-badging.txt
grep -q "versionName='9.2.3-sohcs-gi.2'" sohcs-gi-badging.txt

# Validate that the toon renderer and scene-wide GI were compiled by the Android NDK.
toon_object="$(find Android/app/.cxx -type f -name 'ToonLighting.cpp.o' -print -quit)"
world_light_object="$(find Android/app/.cxx -type f -name 'WorldLighting.cpp.o' -print -quit)"
gi_object="$(find Android/app/.cxx -type f -name 'GlobalIllumination.cpp.o' -print -quit)"
test -s "${toon_object}"
test -s "${world_light_object}"
test -s "${gi_object}"

rm -rf verify-apk
mkdir -p verify-apk
unzip -q "${apk}" 'lib/arm64-v8a/libsoh.so' -d verify-apk
strings verify-apk/lib/arm64-v8a/libsoh.so > sohcs-gi-native-strings.txt

{
    echo "Native renderer validation"
    echo "=========================="
    echo "Toon object: ${toon_object}"
    echo "World lighting object: ${world_light_object}"
    echo "Scene-wide GI object: ${gi_object}"
    echo
    for marker in \
        'Graphics.ToonLighting.Enabled' \
        'Graphics.WorldShadows.Enabled' \
        'Graphics.GlobalIllumination.Enabled' \
        'Graphics.GlobalIllumination.BounceHeight'; do
        grep -a -Fq "${marker}" verify-apk/lib/arm64-v8a/libsoh.so
        echo "PRESENT in packaged libsoh.so: ${marker}"
    done
} | tee artifacts/NATIVE-VALIDATION.txt

cat > artifacts/BUILD-INFO.txt <<EOF
Android branch: ${TARGET_BRANCH}
Roborich celshade: $(git rev-parse roborich-celshade)
Build commit: $(git rev-parse HEAD)
libultraship: $(git -C libultraship rev-parse HEAD)
Package: ${PACKAGE_ID}
Application: ${APP_LABEL}
Version: 9.2.3-sohcs-gi.2 (14)
Data folder: ${DATA_FOLDER}
Compiled Android modules:
- ToonLighting.cpp.o
- WorldLighting.cpp.o
- GlobalIllumination.cpp.o
GI implementation:
- Scene depth + stencil volumes
- Environment indirect light affects map and opaque actors
- Ground bounce affects map and opaque actors
- No full-screen 2D GI overlay
Package validation:
- ARM64 libsoh.so present
- Toon Lighting, World Lighting, Actor Shadows and scene-wide GI CVars present
- Package, version and application label verified by aapt
EOF

# Do not push a generated merge commit from a pull-request workflow.
echo "SOHCS GI v2 APK built and validated successfully."
