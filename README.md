# cpp-blackmagic

`cpp-blackmagic` is a C++20 experimental framework for:
- function decorators
- runtime hooking
- lightweight dependency injection

Main capabilities:
- `decorator(@xxx)` style source markers
- hook backends via MinHook (Windows) and Dobby (Linux/Android)
- `@inject + Depends(...)` parameter injection flow

Current status is closer to an engineering prototype than a production-ready general-purpose library.

## Docs
- [decorator](cpp-blackmagic/docs/decorator.md)
- [depends](cpp-blackmagic/docs/depends.md)

## Repository Layout
- `cpp-blackmagic/include/cppbm/decorator.h`: public decorator entry API
- `cpp-blackmagic/include/cppbm/depends.h`: public DI entry API
- `cpp-blackmagic/include/cppbm/internal/hook/*`: hook pipeline and hook error model
- `cpp-blackmagic/include/cppbm/internal/depends/*`: DI context, registry, resolver, error model
- `cpp-blackmagic/scripts/decorator.py`: preprocess for `decorator(...)`
- `cpp-blackmagic/scripts/inject.py`: preprocess for `@inject` default-arg metadata
- `cpp-blackmagic/scripts/cmake/preprocess.cmake`: CMake integration helpers
- `test/src/main.cpp`: example executable source
- `cpp-blackmagic/docs/decorator.md`: decorator deep dive
- `cpp-blackmagic/docs/depends.md`: depends/inject deep dive

## Requirements
- C++20 compiler
- CMake >= 3.10
- Python 3 (required by CMake preprocess step)
- Python packages for preprocess scripts:
  - `tree_sitter`
  - `tree_sitter_cpp`
  - optional fallback: `tree_sitter_languages`

Install example:

```bash
pip install tree_sitter tree_sitter_cpp tree_sitter_languages
```

## Build

```bash
mkdir build
cd build
cmake -DBUILD_LINUX_X86_64=ON ..
make
```

## Enable In Your Target
In your own `CMakeLists.txt`:

```cmake
set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)
include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(your-target main.cpp)

CPPBM_ENABLE_DECORATOR(TARGET your-target)
CPPBM_ENABLE_DEPENDENCY_INJECT(TARGET your-target)

target_link_libraries(your-target PRIVATE cpp-blackmagic)
```

## Strict Parser Mode (recommended for CI)
By default, preprocess scripts can fallback to regex scanning if tree-sitter is unavailable or parse fails.
Enable strict mode to fail fast:

```cmake
set(CPPBM_PREPROCESS_STRICT_PARSER ON)
```

This option is defined in `cpp-blackmagic/scripts/cmake/preprocess.cmake`.

## Example Coverage
`test/src/main.cpp` demonstrates:
- `decorator(@logger)` for free functions
- `decorator(@inject)` with `Depends(...)`
- `ScopeOverrideDependency<&target>(...)` scoped override

## Error Policy

### Hook side
Defined in `cpp-blackmagic/include/cppbm/internal/hook/error.h`:
- `HookFailPolicy` (default: `Ignore`)
- `SetHookFailPolicy(...)`
- `SetHookErrorCallback(...)`
- `GetLastHookError()`

### Inject side
Defined in `cpp-blackmagic/include/cppbm/internal/depends/error.h`:
- `InjectFailPolicy` (default: `Terminate`)
- `SetInjectFailPolicy(...)`
- `SetInjectErrorCallback(...)`
- `InjectException`

## Known Limitations
- `decorator(@...)` binding uses nearest-following-function rule in preprocess stage and can be ambiguous with tricky source layout.
- Member-function address conversion relies on ABI assumptions.
- DI context is thread-local by default and does not auto-propagate across threads.
- The repo currently focuses on executable examples and does not provide a full assertion-based regression suite yet.


