# Preprocess Integration Guide (CMake / MSBuild)

Decorator and inject behavior depends on preprocess scripts.

- `decorator.py`: handles `decorator(@...)`
- `inject.py`: loaded as a module via `decorator.py --modules inject`
- `invoker.py`: optional module that appends default-call invoker metadata

Without preprocess, `decorator(...)` is just a marker macro.

## 1. Requirements

- Python 3
- Python packages: `tree_sitter`, `tree_sitter_cpp`

```bash
pip install tree_sitter tree_sitter_cpp
```

## 2. CMake integration (recommended)

### 2.1 Decorator only

```cmake
set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)
include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(my_app src/main.cpp)
CPPBM_ENABLE_DECORATOR(TARGET my_app)
target_link_libraries(my_app PRIVATE cpp-blackmagic)
```

### 2.2 Enable inject

```cmake
CPPBM_ENABLE_DECORATOR(TARGET my_app MODULES inject)
```

### 2.3 Multiple modules

```cmake
CPPBM_ENABLE_DECORATOR(TARGET my_app MODULES inject your_module)
```

Practical example:

```cmake
CPPBM_ENABLE_DECORATOR(TARGET my_app MODULES inject invoker)
```

`invoker` is useful for route-style binders that accept invoker metadata and
want preprocess to provide a default no-arg invoker for eligible targets.

## 3. MSBuild integration

Use two files:

- `scripts/msbuild/preprocess.props` for default variables
- `scripts/msbuild/preprocess.targets` for preprocessing and compile-input swap

In `.vcxproj`, import `preprocess.targets` only:

```xml
<ImportGroup Label="ExtensionTargets">
  <Import Project="$(MSBuildProjectDirectory)\thirdparty\scripts\msbuild\preprocess.targets"
          Condition="Exists('$(MSBuildProjectDirectory)\thirdparty\scripts\msbuild\preprocess.targets')" />
</ImportGroup>
```

`preprocess.targets` will auto-import `preprocess.props` when available.

## 4. Common MSBuild properties

Set in `.vcxproj` or `preprocess.props`:

```xml
<PropertyGroup>
  <CppbmEnable>true</CppbmEnable>
  <CppbmPythonExe>python</CppbmPythonExe>
  <CppbmPythonArgs></CppbmPythonArgs>
  <CppbmModules>inject</CppbmModules>
  <CppbmDecoratorArgs></CppbmDecoratorArgs>
  <CppbmDisableFastUpToDateCheck>true</CppbmDisableFastUpToDateCheck>
</PropertyGroup>
```

Notes:

- `CppbmModules` accepts comma or semicolon separators
- `CppbmDisableFastUpToDateCheck=true` helps avoid stale "up-to-date" results in VS

## 5. Generated outputs

- CMake: `<build>/cppbm-gen/<target>/...`
- MSBuild: `$(IntDir)cppbm\...\*.cppbm.gen.cpp`

Treat generated files as build artifacts.

## 6. Troubleshooting checklist

### Marker has no effect

- confirm preprocess is enabled for the target
- inspect generated source and verify `inline auto __cppbm_dec_...` bindings exist

### Inject code not generated

- check `CppbmModules` / `MODULES` includes `inject`
- verify `inject.py` exists in script directory

### IDE build differs from terminal build

- IDE might be using a different Python
- pin `CppbmPythonExe` and `CppbmPythonArgs`

### Duplicate preprocessing

Do not enable both CMake and MSBuild preprocessing for the same target.
