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
    R Call(Args... args) override
    {
        // before
        auto result = this->CallOriginal(args...);
        // after
        return result;
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

## 3. Expression decorators (registration style)

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

## 4. Multiple decorators on one function

Supported:

```cpp
decorator(@inject, @logger)
int foo(...);
```

Practical advice:

- keep markers immediately above the function they decorate
- avoid spreading markers far away from the target declaration/definition

## 5. Common mistakes

### 5.1 Preprocess not enabled

`decorator(...)` macro itself is a no-op. No preprocess means no runtime binding.

### 5.2 Invalid marker syntax

Current expected form is `decorator(@xxx)`.

- `@` is required
- for multiple entries, each argument must start with `@`

### 5.3 Ambiguous marker placement

Binding is based on nearest following function node. Unclear layout can bind to an unexpected function.

## 6. Reference example

- [decorator example](../examples/src/decorator_example.cpp)
