#!/usr/bin/env python3
from pathlib import Path

HEADER = Path("libultraship/include/ship/Context.h")
SOURCE = Path("libultraship/src/ship/Context.cpp")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"Unable to patch {label}: expected source block was not found")
    return text.replace(old, new, 1)


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
print("Roborich libultraship Android data-root integration applied successfully.")
