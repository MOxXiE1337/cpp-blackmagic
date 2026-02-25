# preprocess Guide

This document explains how to enable source preprocessing for `decorator.py` and `inject.py` in two environments:
- CMake flow (`scripts/cmake/preprocess.cmake`)
- Visual Studio MSBuild flow (`scripts/msbuild/preprocess.targets`)

Use one flow per target. Do not enable both for the same compile target.

## Prerequisites
- Python 3 available in build environment
- Python packages:
  - `tree_sitter`
  - `tree_sitter_cpp`
  - optional fallback: `tree_sitter_languages`

Install example:

```bash
pip install tree_sitter tree_sitter_cpp tree_sitter_languages
```

## CMake Flow
File: `cpp-blackmagic/scripts/cmake/preprocess.cmake`

### 1) Include helper and enable passes
In your target `CMakeLists.txt`:

```cmake
set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)
include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(my-app src/main.cpp)

CPPBM_ENABLE_DECORATOR(TARGET my-app)
CPPBM_ENABLE_DEPENDENCY_INJECT(TARGET my-app)

target_link_libraries(my-app PRIVATE cpp-blackmagic)
```

### 2) Optional strict parser mode
By default strict mode is `OFF`.

```cmake
set(CPPBM_PREPROCESS_STRICT_PARSER ON)
```

When `ON`, preprocessing fails immediately if tree-sitter parser is unavailable or parse fails.

### 3) Output behavior
- Decorator pass writes generated sources to:
  - `${CMAKE_BINARY_DIR}/cppbm-gen/<target>/<relative-source-path>`
- Inject pass runs on those generated sources in place.
- Final compile input is replaced by generated files.

### 4) Important ordering
Call order should be:
1. `CPPBM_ENABLE_DECORATOR`
2. `CPPBM_ENABLE_DEPENDENCY_INJECT`

This ensures inject metadata is generated from decorator-processed sources.

## Visual Studio MSBuild Flow
File: `cpp-blackmagic/scripts/msbuild/preprocess.targets`

This flow compiles generated files from `$(IntDir)` and keeps original source files untouched.

### 1) Import targets into `.vcxproj`
Add before `</Project>`:

```xml
<Import Project="$(MSBuildProjectDirectory)\cpp-blackmagic\scripts\msbuild\preprocess.targets"
        Condition="Exists('$(MSBuildProjectDirectory)\cpp-blackmagic\scripts\msbuild\preprocess.targets')" />
```

### 2) Optional properties
You can define these in `.vcxproj` or VS User Macros:

```xml
<PropertyGroup>
  <CppbmPythonExe>py</CppbmPythonExe>
  <CppbmPythonArgs>-3.9</CppbmPythonArgs>
  <CppbmScriptDir>$(MSBuildProjectDirectory)\cpp-blackmagic\scripts\</CppbmScriptDir>
  <CppbmStrictArg>--strict-parser</CppbmStrictArg>
  <CppbmDisableFastUpToDateCheck>true</CppbmDisableFastUpToDateCheck>
</PropertyGroup>
```

Notes:
- `CppbmPythonExe` default: `python`
- `CppbmPythonArgs` default: empty
- `CppbmStrictArg` default: `--strict-parser`
- `CppbmDisableFastUpToDateCheck` default: `true`

### 3) Output behavior
For each `ClCompile` input (`.cpp/.cc/.cxx`):
- decorator output:
  - `$(IntDir)cppbm\<RelativeDir>\<Filename>.cppbm.decor.cpp`
- inject output:
  - `$(IntDir)cppbm\<RelativeDir>\<Filename>.cppbm.gen.cpp`

Then MSBuild swaps compile inputs:
- removes original `ClCompile` inputs
- compiles generated `.cppbm.gen.cpp` files

Incremental behavior:
- preprocessing runs on each MSBuild invocation
- changes in original `.cpp` are picked up on next Build (no need to manually regenerate solution)
- script updates are also picked up on next Build

### 4) Source editing experience
- Original `.cpp` files are not overwritten.
- You continue editing original files in IDE.
- Generated files are build artifacts under `$(IntDir)`.

## Encoding Notes
`decorator.py` and `inject.py` support auto-detection for common encodings (including UTF-16 LE/BE with BOM) and preserve source encoding on output.

This helps in mixed environments (for example, Visual Studio saving sources in UTF-16).

## Troubleshooting
1. `decorator.py not found` / `inject.py not found`
- Verify `CppbmScriptDir` (MSBuild) or include path (CMake).

2. Parse failures in strict mode
- Ensure tree-sitter packages are installed in the Python interpreter used by build.

3. Works in shell, fails in IDE
- IDE may use a different Python. Pin interpreter via:
  - CMake: `Python3_EXECUTABLE`
  - MSBuild: `CppbmPythonExe` + `CppbmPythonArgs`

4. Double preprocessing symptoms
- Do not enable both CMake preprocess and MSBuild preprocess for the same target.

5. VS says "up to date" but source changed
- Keep `CppbmDisableFastUpToDateCheck=true` (default in `preprocess.targets`)
- If you intentionally want VS fast check, set it to `false` and verify your project up-to-date settings carefully.
