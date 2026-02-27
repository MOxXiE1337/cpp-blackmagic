set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)

if (NOT EXISTS "${CPPBM_PREPROCESS_CMAKE}")
    message(FATAL_ERROR "Missing preprocess.cmake: ${CPPBM_PREPROCESS_CMAKE}")
endif ()

include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(cppbm-example-decorator
    src/decorator/cases.h
    src/decorator/main.cpp
    src/decorator/case_basic.cpp
    src/decorator/case_fixed_target.cpp
    src/decorator/case_expression.cpp
    src/decorator/case_context.cpp
    src/decorator/case_chain.cpp
    src/decorator/case_member.cpp
)

CPPBM_ENABLE_DECORATOR(TARGET cppbm-example-decorator)
target_link_libraries(cppbm-example-decorator PRIVATE cpp-blackmagic)

add_executable(cppbm-example-depends
    src/depends_example.cpp
)

CPPBM_ENABLE_DECORATOR(TARGET cppbm-example-depends MODULES inject)
target_link_libraries(cppbm-example-depends PRIVATE cpp-blackmagic)
