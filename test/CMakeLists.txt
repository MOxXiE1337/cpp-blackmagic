set(CPPBM_PREPROCESS_CMAKE
    "${PROJECT_SOURCE_DIR}/cpp-blackmagic/scripts/cmake/preprocess.cmake"
)

if (NOT EXISTS "${CPPBM_PREPROCESS_CMAKE}")
    message(FATAL_ERROR "Missing preprocess.cmake: ${CPPBM_PREPROCESS_CMAKE}")
endif ()

include("${CPPBM_PREPROCESS_CMAKE}")

add_executable(cpp-blackmagic-test 
    test.h  
    inlinehook.h
    main.cpp)

# Enable our custom syntax
CPPBM_ENABLE_PREPROCESS(TARGET cpp-blackmagic-test)

target_include_directories(cpp-blackmagic-test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/cpp-blackmagic/src
)
target_link_libraries(cpp-blackmagic-test PRIVATE cpp-blackmagic)
