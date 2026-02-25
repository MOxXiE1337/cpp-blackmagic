# decorator.h Guide

## Scope
This document explains how `cpp-blackmagic/include/cppbm/decorator.h` works, what it generates, and how to use it safely.

`decorator.h` provides:
- decorator marker syntax (`decorator(@name)`)
- class decorator syntax (`decorator(@router.get("/path"))`)
- decorator class naming helper (`decorator_class(name)`)
- function/member-function hook wrappers (`FunctionDecorator<Target>`)
- integration with hook runtime (`internal/hook/hook.h`)

## Build-Time Requirement
`decorator(...)` is not a real C++ attribute. It is consumed by the preprocess step in `cpp-blackmagic/scripts/decorator.py`.

Typical CMake integration:

```cmake
include("cpp-blackmagic/scripts/cmake/preprocess.cmake")
CPPBM_ENABLE_DECORATOR(TARGET your_target)
```

Optional strict parser mode:

```cmake
set(CPPBM_PREPROCESS_STRICT_PARSER ON)
```

When strict mode is enabled, preprocess fails if tree-sitter C++ parsing is unavailable/fails, instead of falling back to regex matching.

## Main Concepts

### 1) Decorator Marker
In source code:

```cpp
decorator(@logger)
int add(int a, int b);
```

The macro in `decorator.h` is intentionally a no-op:

```cpp
#define decorator(t) /* t */
```

Binding is done by the Python preprocessor, not by the C++ preprocessor.

### 2) Decorator Class Naming Convention (Symbol Form)
`decorator_class(name)` expands to `_Decorator_<name>_`.

Example:

```cpp
template <auto Target>
class decorator_class(logger);
```

becomes:

```cpp
template <auto Target>
class _Decorator_logger_;
```

The preprocess output instantiates this class and binds it to target functions.

### 3) Class Decorator (RouteBinder Form)
Besides `decorator(@name)`, you can use class decorator syntax:

```cpp
decorator(@router.get("/health"))
int health();
```

For this form, preprocess generates:

```cpp
inline auto __cppbm_health_dec_xx = (router.get("/health")).bind<&health>();
```

So the expression result must expose:

```cpp
template <auto Target>
auto bind() const;
```

`bind<&Target>()` can return any type that makes this generated statement valid.
The framework does not enforce a specific return type.

Common patterns:
- Return a decorator object (for hook-style behavior, often based on `FunctionDecorator<Target>`).
- Return a status value such as `bool` (for registration-style side effects, e.g. router tables).

The key requirement is that `bind<&Target>()` itself performs or triggers the behavior you expect.

### 4) FunctionDecorator<Target>
`FunctionDecorator<Target>` is the runtime base class that installs hooks.

- Constructor installs hook immediately.
- Destructor uninstalls hook (through base pipeline RAII).
- Works for free functions and member functions (including const member functions).

## Call Flow
Runtime dispatch is defined in `internal/hook/hook.h`:

1. Detour receives the call.
2. `BeforeCall(args...)` runs.
3. `Call(args...)` runs.
4. `AfterCall()` runs via scope-exit guard, including exception paths.
5. `CallOriginal(...)` is available to invoke original function trampoline.

Default behavior:
- `BeforeCall` returns `true`.
- `Call` forwards to `CallOriginal`.
- `AfterCall` does nothing.

Override only what you need.

## Supported Targets
- Free function pointer: `R(*)(Args...)`
- Member function pointer: `R(C::*)(Args...)`
- Const member function pointer: `R(C::*)(Args...) const`
- On MSVC x86 path: explicit support for `__stdcall` and `__fastcall` free-function hooks.

## Hook Error Mechanism
Hook failure control lives in `cpp-blackmagic/include/cppbm/internal/hook/error.h`.

Key types:
- `hook::HookErrorCode`
- `hook::HookError`
- `hook::HookFailPolicy`
- `hook::HookException`

Key APIs:
- `hook::SetHookFailPolicy(...)`
- `hook::GetHookFailPolicy()`
- `hook::SetHookErrorCallback(...)`
- `hook::GetHookErrorCallback()`
- `hook::GetLastHookError()`
- `hook::ClearLastHookError()`

Current default policy is `HookFailPolicy::Ignore`:
- hook install failure returns `false`
- no exception by default
- last error is stored as thread-local state

If you want fail-fast behavior:

```cpp
cpp::blackmagic::hook::SetHookFailPolicy(
    cpp::blackmagic::hook::HookFailPolicy::Throw
);
```

## Minimal Example

```cpp
#include <cppbm/decorator.h>
#include <cstdio>

template <auto Target>
class decorator_class(logger);

template <typename R, typename... Args, R(*Target)(Args...)>
class decorator_class(logger)<Target>
    : public cpp::blackmagic::FunctionDecorator<Target>
{
public:
    bool BeforeCall(Args... args) override
    {
        std::puts("before");
        return true;
    }

    R Call(Args... args) override
    {
        return this->CallOriginal(args...);
    }

    void AfterCall() override
    {
        std::puts("after");
    }
};

decorator(@logger)
int add(int a, int b)
{
    return a + b;
}
```

## Class Decorator Example (Router + RouteBinder)

```cpp
#include <cppbm/decorator.h>
#include <cstdio>
#include <string>
#include <utility>

template <auto Target>
class RouteDecorator;

template <typename R, typename... Args, R(*Target)(Args...)>
class RouteDecorator<Target> : public cpp::blackmagic::FunctionDecorator<Target>
{
public:
    RouteDecorator(std::string method, std::string path)
        : method_(std::move(method)), path_(std::move(path)) {}

    bool BeforeCall(Args...) override
    {
        std::printf("[route] %s %s\n", method_.c_str(), path_.c_str());
        return true;
    }

private:
    std::string method_;
    std::string path_;
};

class RouteBinder
{
public:
    RouteBinder(std::string method, std::string path)
        : method_(std::move(method)), path_(std::move(path)) {}

    template <auto Target>
    auto bind() const
    {
        return RouteDecorator<Target>{ method_, path_ };
    }

private:
    std::string method_;
    std::string path_;
};

class Router
{
public:
    RouteBinder get(const char* path) const
    {
        return RouteBinder{ "GET", path };
    }
};

inline Router router{};

decorator(@router.get("/health"))
int health()
{
    return 200;
}
```

## Operational Notes and Limits
- Decorator binding is based on "nearest following function" in preprocess output. Keep markers close to the intended target and avoid ambiguous layouts.
- Member function address conversion uses pointer-representation assumptions. Complex class layouts/ABIs may not always be safe.
- Hook state (`self_`, `original_`) is static per decorator specialization. Design with one active binding instance per specialization in mind.
- If preprocess is skipped, marker syntax will not produce runtime binding.
