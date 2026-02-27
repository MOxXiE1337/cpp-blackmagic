# Decorator Guide

This guide is usage-focused: what to write, what is generated, and what commonly goes wrong.

## 1. Prerequisite: enable preprocess

`decorator(@...)` is consumed by preprocess scripts.

Minimal CMake setup:

```cmake
set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)
include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(my_app src/main.cpp)
CPPBM_ENABLE_DECORATOR(TARGET my_app)
target_link_libraries(my_app PRIVATE cpp-blackmagic)
```

## 2. Standard function decorator workflow

### 2.1 Define your decorator class

```cpp
#include <cppbm/decorator.h>

using namespace cpp::blackmagic;

template <auto Target>
class LoggerDecorator;

template <typename R, typename... Args, R(*Target)(Args...)>
class LoggerDecorator<Target> : public FunctionDecorator<Target>
{
public:
    bool BeforeCall(Args&... /*args*/) override
    {
        // before logic
        return true;
    }

    void AfterCall(R& /*result*/) override
    {
        // after logic
    }
};
```

### 2.2 Expose a binder object

```cpp
CPPBM_DECORATOR_BINDER(LoggerDecorator, logger);
```

### 2.3 Mark target function

```cpp
decorator(@logger)
int add(int a, int b)
{
    return a + b;
}
```

## 3. CallContext (per-decorator call state)

Use `CallContext` only when your decorator needs per-call state shared between
`Before...` and `After...`.

### 3.1 Rules

- Override `ContextSize()` to reserve context bytes for your decorator.
- Construct and destroy your state explicitly (`std::construct_at` / `std::destroy_at`).
- If `ContextSize()` returns `0`, `CallContext` has no usable storage.

### 3.2 Example

```cpp
#include <cppbm/decorator.h>

#include <chrono>
#include <cstdint>
#include <memory>

using namespace cpp::blackmagic;

template <auto Target>
class TimingDecorator;

template <typename R, typename... Args, R(*Target)(Args...)>
class TimingDecorator<Target> : public FunctionDecorator<Target>
{
public:
    struct Frame
    {
        std::int64_t start_ns = 0;
    };

    std::size_t ContextSize() const override
    {
        return sizeof(Frame);
    }

    bool BeforeCall(hook::CallContext& ctx, Args&... /*args*/) override
    {
        auto* frame = ctx.As<Frame>();
        if (frame == nullptr)
        {
            return false;
        }
        std::construct_at(frame, Frame{ NowNs() });
        return true;
    }

    void AfterCall(hook::CallContext& ctx, R& /*result*/) override
    {
        auto* frame = ctx.As<Frame>();
        if (frame == nullptr)
        {
            return;
        }
        const auto elapsed_ns = NowNs() - frame->start_ns;
        std::destroy_at(frame);
        (void)elapsed_ns;
    }

private:
    static std::int64_t NowNs()
    {
        using Clock = std::chrono::steady_clock;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count();
    }
};
```

Note: each dispatch invocation has isolated context storage, so nested decorated calls
do not overwrite each other.

## 4. Expression decorators (registration style)

You can decorate with an expression:

```cpp
decorator(@router.get("/health"))
int health();
```

Requirement: expression result must provide:

```cpp
template <auto Target>
auto Bind() const;
```

So `router.get(...)` can return any binder-like object; return type does not need to be a decorator class.

### 4.1 Bind metadata (`Bind<&Target>(metas...)`)

Binders may optionally accept metadata arguments:

```cpp
template <auto Target, typename... Metas>
auto Bind(Metas&&... metas) const;
```

Preprocess modules can append metadata to one `Bind` call. Typical examples:

- inject metadata: `depends::InjectArgMeta<Index, Param>(...)`
- invoker metadata: `[]() { return ::QualifiedTarget(); }`

Consumption is binder-specific:

- `InjectBinder` only applies inject metadata and ignores unknown metadata.
- route-style binders can pick invoker metadata and ignore the rest.
- generic `DecoratorBinder` ignores metadata by default.

This allows mixed usage such as `decorator(@inject, @router.get("/x"))` without forcing one binder to parse all metadata kinds.

## 5. Multiple decorators on one function

Supported:

```cpp
decorator(@inject, @logger)
int foo(...);
```

Practical advice:

- keep markers immediately above the function they decorate
- avoid spreading markers far away from the target declaration/definition

## 6. Common mistakes

### 6.1 Preprocess not enabled

`decorator(...)` macro itself is a no-op. No preprocess means no runtime binding.

### 6.2 Invalid marker syntax

Current expected form is `decorator(@xxx)`.

- `@` is required
- for multiple entries, each argument must start with `@`

### 6.3 Ambiguous marker placement

Binding is based on nearest following function node. Unclear layout can bind to an unexpected function.

## 7. Reference examples

- [decorator example entry](../examples/src/decorator/main.cpp)
- [basic binder case](../examples/src/decorator/case_basic.cpp)
- [fixed target case](../examples/src/decorator/case_fixed_target.cpp)
- [expression binder case](../examples/src/decorator/case_expression.cpp)
- [context case](../examples/src/decorator/case_context.cpp)
- [chain case](../examples/src/decorator/case_chain.cpp)
- [member-function case](../examples/src/decorator/case_member.cpp)
