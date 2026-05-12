cmake_minimum_required(VERSION 3.21)

project(aerosentinel_control_center
    VERSION 1.0.0
    DESCRIPTION "AeroSentinel Control Center built with Drogon C++"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)

add_executable(aerosentinel-control
    src/main.cc)

target_link_libraries(aerosentinel-control
    PRIVATE
        Drogon::Drogon)

target_compile_definitions(aerosentinel-control
    PRIVATE
        AEROSENTINEL_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_compile_definitions(aerosentinel-control
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN)
endif()

include(CTest)

if(BUILD_TESTING AND UNIX)
    find_program(BASH_EXECUTABLE bash)
    find_program(CURL_EXECUTABLE curl)

    if(BASH_EXECUTABLE AND CURL_EXECUTABLE)
        add_test(
            NAME aerosentinel_http_smoke
            COMMAND
                "${BASH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/smoke-test.sh"
                "$<TARGET_FILE:aerosentinel-control>")

        set_tests_properties(aerosentinel_http_smoke
            PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                TIMEOUT 30)
    endif()
endif()

add_custom_command(TARGET aerosentinel-control POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/public"
        "$<TARGET_FILE_DIR:aerosentinel-control>/public")

set(_aerosentinel_install_targets aerosentinel-control)

if(WIN32)
    enable_language(RC)
    find_package(unofficial-webview2 CONFIG REQUIRED)

    add_executable(aerosentinel-desktop WIN32
        src/desktop_win.cc
        resources/windows/aerosentinel.rc)

    target_compile_definitions(aerosentinel-desktop
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            UNICODE
            _UNICODE)

    target_link_libraries(aerosentinel-desktop
        PRIVATE
            unofficial::webview2::webview2
            ole32
            shell32
            user32
            ws2_32)

    add_dependencies(aerosentinel-desktop aerosentinel-control)
    list(APPEND _aerosentinel_install_targets aerosentinel-desktop)
endif()

install(TARGETS ${_aerosentinel_install_targets}
    RUNTIME_DEPENDENCY_SET aerosentinel_runtime_deps
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION lib)

install(DIRECTORY public DESTINATION .)

set(_aerosentinel_runtime_dirs)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _aerosentinel_runtime_dirs
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
endif()

set(_aerosentinel_runtime_args)
if(_aerosentinel_runtime_dirs)
    list(APPEND _aerosentinel_runtime_args DIRECTORIES ${_aerosentinel_runtime_dirs})
endif()

install(RUNTIME_DEPENDENCY_SET aerosentinel_runtime_deps
    ${_aerosentinel_runtime_args}
    PRE_EXCLUDE_REGEXES
        "api-ms-.*"
        "ext-ms-.*"
        "^[Ww][Tt][Dd][Cc][Cc][Mm]\\.dll$"
    POST_EXCLUDE_REGEXES
        ".*[Ww]indows/[Ss]ystem32/.*"
        "^/lib/.*"
        "^/usr/lib/.*"
        "^/System/Library/.*"
    DESTINATION .)
