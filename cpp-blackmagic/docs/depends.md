# Depends DI Guide (Runtime + Internals)

This document explains the current dependency-injection system used by
`cppbm/depends.h`, including:

- public APIs (`Depends`, `InjectDependency`, `ScopeOverrideDependency`)
- preprocess-generated metadata pipeline (`decorator.py` + `inject.py`)
- runtime resolution order (sync and async)
- context and lifetime model (contextvar-style state + per-call lease)
- coroutine support with `Task<T>`
- known limits and troubleshooting

## 1. Quick Start

### 1.1 Enable Preprocess in CMake

```cmake
include("cpp-blackmagic/scripts/cmake/preprocess.cmake")
CPPBM_ENABLE_DECORATOR(TARGET your_target)
CPPBM_ENABLE_DEPENDENCY_INJECT(TARGET your_target)
```

Optional strict parser mode:

```cmake
set(CPPBM_PREPROCESS_STRICT_PARSER ON)
```

### 1.2 Minimal Usage

```cpp
#include <cppbm/depends.h>

using namespace cpp::blackmagic;

struct Config
{
    const char* env = "prod";
};

Config& ConfigFactory()
{
    static Config c{ "prod" };
    return c;
}

decorator(@inject)
const char* ReadEnv(Config& cfg = Depends(ConfigFactory))
{
    return cfg.env;
}
```

## 2. Public API Surface

### 2.1 `Depends(...)`

```cpp
Depends(bool cached = true)
Depends(factory, bool cached = true)
```

Factory requirements:

- zero-argument function pointer
- returns pointer/reference directly, or task-like value with `Get()`
  that eventually resolves to pointer/reference

`cached` behavior:

- `true`: allows reusing an already resolved slot in current/parent chain
- `false`: forces fresh resolve into current call context

### 2.2 Explicit Injection

Global:

```cpp
InjectDependency(value)
InjectDependency(value, factory)
```

Target-scoped:

```cpp
InjectDependency<&Target>(value)
InjectDependency<&Target>(value, factory)
```

Accepted explicit handle types:

- pointer
- `std::reference_wrapper<T>`

### 2.3 Cleanup / Removal

```cpp
ClearDependencies();
ClearDependencies<&Target>();

RemoveDependency<T>();
RemoveDependency<T>(factory);
RemoveDependency<&Target, T>();
RemoveDependency<&Target, T>(factory);

RemoveDependencyAt(target_key, factory_key, type_index);
```

### 2.4 Scoped Override (RAII)

```cpp
auto g1 = ScopeOverrideDependency(value);
auto g2 = ScopeOverrideDependency(value, factory);
auto g3 = ScopeOverrideDependency<&Target>(value);
auto g4 = ScopeOverrideDependency<&Target>(value, factory);
```

Guard destruction restores previous explicit value (or removes it).

## 3. Preprocess Pipeline

Two scripts cooperate:

- `scripts/decorator.py`:
  - removes `decorator(...)` markers
  - appends generated decorator-binding instances
- `scripts/inject.py`:
  - scans `@inject` targets
  - appends default-argument metadata registration

Generated metadata entries are registered by:

```cpp
cpp::blackmagic::depends::InjectRegistry::Register<&Target, ParamIndex>(...)
```

For each default argument on an `@inject` target, `inject.py` emits:

- one sync metadata registration
- one async metadata registration

Async resolver prefers async entries and falls back to sync entries.

## 4. Core Runtime Architecture

### 4.1 Main Components

- `internal/depends/placeholder.h`
  - `Depends` marker protocol
- `internal/depends/maker.h`
  - `DependsMaker` / `DependsMakerWithFactory`
- `internal/depends/meta.h`
  - metadata conversion for default arguments
- `internal/depends/registry.h`
  - explicit registry + default-arg metadata registry
- `internal/depends/context.h`
  - inject state, slot chain, context lease, contextvar binding
- `internal/depends/resolve.h`
  - sync/async resolution logic
- `internal/depends/coroutine.h`
  - scheduler-backed `Task<T>`
- `depends.h`
  - public APIs + `@inject` call resolver/invoker glue

### 4.2 Key Runtime Objects

- `InjectContextState`
  - `root` context
  - `context_stack`
  - execution depth counters
- `InjectContext`
  - parent pointer
  - map of local resolved slots
- `ContextSlot`
  - raw object pointer
  - optional holder for owned lifetime

## 5. Placeholder and Metadata Model

`Depends(...)` in function declaration does not directly resolve dependencies.
It first yields a marker-compatible value.

At runtime (`@inject` wrapper):

1. detect whether an argument is a placeholder
2. lookup generated metadata by `(target, parameter index, metadata type)`
3. resolve dependency (with explicit injection override support)
4. write back/capture resolved value

Metadata payload for pointer/reference dependency defaults:

```cpp
DependsPtrValue<T>{
    T* ptr,
    bool owned,
    const void* factory,
    bool cached
}
```

## 6. Registry Design

### 6.1 Explicit Value Registry

Key:

- target key (`nullptr` for global)
- factory key (`nullptr` for plain Depends)
- requested type (`std::type_index`)

Lookup policy:

- first target-specific exact factory key
- then global exact factory key
- no implicit fallback from factory-specific key to `nullptr` factory key

### 6.2 Default-Argument Metadata Registry

Key:

- target key
- parameter index
- metadata value type (`std::type_index`)

Value:

- erased factory `std::function<std::any()>`

Move-only metadata (`Task<...>`) handling:

- `std::any` cannot hold move-only objects directly
- registry stores boxed `std::shared_ptr<U>` for move-only `U`
- extraction path supports unboxing and moving out

## 7. Resolution Order

For raw dependency slot resolution (`EnsureRawSlot`):

1. find cached slot in current/parent context chain (when `cached == true`)
2. try explicit injection (`InjectDependency`) by key
3. optional default construction (only when allowed and default-constructible)

For `@inject` default argument resolution:

- pointer/reference default from metadata:
  - can populate slot from explicit override first
  - then fallback to metadata-produced pointer/reference
  - honor `owned` and `cached`
- value default from metadata:
  - assign/copy/move to argument
  - may cache resolved value when appropriate

## 8. Context and Lifetime Semantics

### 8.1 Contextvar-Style Active State

`context.h` uses `utils::ContextVar<std::shared_ptr<InjectContextState>>`.

Behavior:

- synchronous path:
  - active context var usually unset
  - falls back to thread-local ambient state
- coroutine path:
  - resume edge binds state owner for that step
  - active state follows coroutine scheduling edges

### 8.2 Per-call Lease

Each `@inject` call acquires `InjectContextLease`:

- top-level call gets isolated state owner
- nested `@inject` call reuses same state owner and pushes child frame
- lease destruction removes its local frame

This allows:

- nested `@inject` to share parent cache chain
- top-level requests to stay isolated from each other

### 8.3 Ownership Rules

- explicit injected pointers/reference_wrappers are borrowed
- `Depends(factory)` returning pointer implies owned slot
- `Depends(factory)` returning reference implies borrowed slot
- plain `Depends()` resolves borrowed/owned according to resolver path

## 9. Coroutine Support (`Task<T>`)

User-facing alias:

```cpp
cpp::blackmagic::Task<>      // same as Task<void>
cpp::blackmagic::Task<int>
cpp::blackmagic::Task<T&>
```

Implementation:

- scheduler-backed, per-thread queue
- `Task::Get()` pumps scheduler until completion
- child task resumes parent from child `final_suspend`

Important safety behavior:

- `co_await` supports both lvalue and rvalue tasks
- rvalue `co_await` path transfers coroutine handle ownership to awaiter
  to avoid temporary-destruction invalidation

## 10. Async Depends Behavior

For `@inject` targets returning `Task<...>`:

- resolver uses async path (`TryResolveDefaultArgForParamAsync`)
- default-arg metadata can be `Task<DependsPtrValue<...>>` or `Task<Param>`
- async metadata is awaited in parameter pipeline
- if async metadata not found, fallback to sync metadata

For non-Task `@inject` targets:

- compatibility path still works
- task-like factory returns may be consumed via `Get()` semantics in conversion flow

### 10.1 `co_return` Factories

Supported:

- `Task<T*>` factory
- `Task<T&>` factory
- normal `T*` / `T&` factory

### 10.2 `co_yield` Factories

Not part of current `Depends` pipeline.
Current design expects single-result dependency factories.

## 11. Type Adaptation Notes

Common case:

- factory returns derived pointer/reference
- parameter declares base pointer/reference

This is supported if C++ conversion is valid.

Example:

```cpp
struct IDatabase { virtual ~IDatabase() = default; };
struct CDatabase : IDatabase {};

CDatabase& MakeDb();

decorator(@inject)
IDatabase* UseDb(IDatabase* db = Depends(MakeDb));
```

## 12. Error Handling

Error model is in `internal/depends/error.h`:

- `InjectErrorCode`
- `InjectError`
- `InjectFailPolicy`
- `InjectException`

Configuration:

```cpp
depends::SetInjectFailPolicy(depends::InjectFailPolicy::Throw);
depends::SetInjectErrorCallback(...);
```

Default policy is terminate.

## 13. Known Limits

- preprocess is required for robust `@inject` metadata behavior
- explicit injection matches registered type key exactly
- factory signature must remain no-arg function pointer
- this DI runtime does not provide thread pool or IO scheduler
  (it only provides coroutine task scheduling glue)
- generator-style multi-yield factory is intentionally not supported

## 14. Debugging Checklist

If dependency is not injected as expected:

1. confirm preprocess is enabled for target
2. inspect generated source in `cppbm-gen/...` for metadata lines
3. verify target function pointer and factory pointer are exactly the same symbol
4. check declared parameter type vs injected value type key
5. verify `cached` behavior and whether parent context already has slot

If async route crashes:

1. inspect stack around `Task::Awaiter` / `TaskScheduler::RunOne`
2. ensure task value is not destroyed before awaited completion
3. confirm injected context is bound (`AutoBindInjectContext` path)

## 15. Example References

- Basic explicit/scope override:
  - `examples/src/depends_example.cpp`
- Async route + async dependency factories:
  - `examples/http-server/http_server_example.cpp`

