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

add_custom_command(TARGET aerosentinel-control POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/public"
        "$<TARGET_FILE_DIR:aerosentinel-control>/public")

install(TARGETS aerosentinel-control
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
    POST_EXCLUDE_REGEXES
        ".*[Ww]indows/[Ss]ystem32/.*"
        "^/lib/.*"
        "^/usr/lib/.*"
        "^/System/Library/.*"
    DESTINATION .)
