set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cpp-blackmagic STATIC
    include/cppbm/internal/utils/noncopyable.h
    include/cppbm/internal/hook/hooker.h
    include/cppbm/internal/hook/error.h
    include/cppbm/internal/hook/hook.h

    include/cppbm/internal/depends/factory_invoke.h
    include/cppbm/internal/depends/placeholder.h
    include/cppbm/internal/depends/registry.h
    include/cppbm/internal/depends/error.h
    include/cppbm/internal/depends/meta.h
    include/cppbm/internal/depends/context.h
    include/cppbm/internal/depends/resolve.h
    include/cppbm/internal/depends/maker.h

    include/cppbm/decorator.h
    include/cppbm/depends.h
    )

target_include_directories(cpp-blackmagic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include
)

# win32/64 hook library
if (WIN32)
    target_sources(cpp-blackmagic
        PRIVATE
        src/internal/hooker/minhook.cpp
    )

    if (BUILD_WINDOWS_X86)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/libMinHook.x86.lib
        )
    endif ()

    if (BUILD_WINDOWS_X86_64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/libMinHook.x64.lib
        )
    endif ()
endif ()

# linux
if(UNIX)
    target_sources(cpp-blackmagic 
        PRIVATE
        src/internal/hooker/dobby.cpp
    )

    # linux
    if(BUILD_LINUX_X86)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/linux.libdobby_x86.a
        )
    endif()

    if(BUILD_LINUX_X86_64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/linux.libdobby_x86_64.a
        )
    endif()

    if(BUILD_LINUX_ARM)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/linux.libdobby_arm.a
        )
    endif()

    if(BUILD_LINUX_ARM64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/linux.libdobby_arm64.a
        )
    endif()
    
    # android
    if(BUILD_ANDROID_X86)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/android.libdobby_x86.a
        )
    endif()

    if(BUILD_ANDROID_X86_64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/android.libdobby_x86_64.a
        )
    endif()

    if(BUILD_ANDROID_ARM)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/android.libdobby_arm.a
        )
    endif()

    if(BUILD_ANDROID_ARM64)
        target_link_libraries(cpp-blackmagic
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/android.libdobby_arm64.a
        )
    endif()
endif()
