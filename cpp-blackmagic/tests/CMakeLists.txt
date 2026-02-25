set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)

if (NOT EXISTS "${CPPBM_PREPROCESS_CMAKE}")
    message(FATAL_ERROR "Missing preprocess.cmake: ${CPPBM_PREPROCESS_CMAKE}")
endif ()

include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(cppbm-test-depends-benchmark
    src/benchmark.cpp
)

CPPBM_ENABLE_DECORATOR(TARGET cppbm-test-depends-benchmark)
CPPBM_ENABLE_DEPENDENCY_INJECT(TARGET cppbm-test-depends-benchmark)
target_link_libraries(cppbm-test-depends-benchmark PRIVATE cpp-blackmagic)

# Intentionally compile-only benchmark target:
# no add_test() here, so cmake --build only compiles and links.
