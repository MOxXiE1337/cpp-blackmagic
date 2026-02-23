add_library(cpp-blackmagic STATIC
    include/cppbm/hooker.h
    include/cppbm/hook.h
    include/cppbm/decorator.h
    include/cppbm/noncopyable.h)


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
