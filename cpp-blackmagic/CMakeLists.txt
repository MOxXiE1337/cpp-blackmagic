add_library(cpp-blackmagic STATIC
    include/cppbm/hooker.h
    include/cppbm/hook.h
    include/cppbm/decorator.h
    include/cppbm/noncopyable.h )


target_include_directories(cpp-blackmagic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include
)

# win32/64 hook library
if (WIN32)
    if (BUILD_WIN32 OR BUILD_WIN64)
        target_sources(cpp-blackmagic
            PRIVATE
                src/internal/hooker/win.cpp
        )
    endif ()

    if (BUILD_WIN32)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/libMinHook.x86.lib
        )
    endif ()

    if (BUILD_WIN64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/libMinHook.x64.lib
        )
    endif ()
endif ()
