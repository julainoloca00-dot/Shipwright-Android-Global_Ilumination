#!/usr/bin/env python3
from pathlib import Path

HEADER = Path("libultraship/include/ship/Context.h")
SOURCE = Path("libultraship/src/ship/Context.cpp")
MOBILE_HEADER_SOURCE = Path("patches/libultraship-android/MobileImpl.h")
MOBILE_SOURCE_SOURCE = Path("patches/libultraship-android/MobileImpl.cpp")
MOBILE_HEADER_TARGET = Path("libultraship/include/ship/port/mobile/MobileImpl.h")
MOBILE_SOURCE_TARGET = Path("libultraship/src/ship/port/mobile/MobileImpl.cpp")


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
print("Roborich libultraship Android data-root and mobile integrations applied successfully.")
