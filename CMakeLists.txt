cmake_minimum_required(VERSION 3.21)
project(KomorebiNext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network LinguistTools)

qt_standard_project_setup()

file(GLOB_RECURSE NEXT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

qt_add_executable(komorebi-lite WIN32 ${NEXT_SOURCES})

# The mark travels with the binary: the window icon comes from a Qt resource, the shell icon from
# a Windows resource script generated with an absolute path so it builds from any directory.
qt_add_resources(komorebi-lite "icons"
    PREFIX "/"
    FILES
        resources/komorebi-lite-256.png
        resources/komorebi-lite.svg
)
if(WIN32)
    configure_file(resources/windows.rc.in ${CMAKE_CURRENT_BINARY_DIR}/windows.rc @ONLY)
    target_sources(komorebi-lite PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/windows.rc)
endif()
# Three languages, compiled in: the source is English, and the two translation files next to it
# carry 简体中文 and 日本語.
qt_add_translations(TARGETS komorebi-lite
    TS_FILES
        i18n/komorebi_zh_CN.ts
        i18n/komorebi_ja.ts
    RESOURCE_PREFIX "/i18n"
)

target_link_libraries(komorebi-lite PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network)
target_include_directories(komorebi-lite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The theme travels with the binary: tokens and the sheet are read at startup, not compiled in.
file(GLOB_RECURSE THEME_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/theme/*)
foreach(file ${THEME_FILES})
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${file})
    configure_file(${file} ${CMAKE_CURRENT_BINARY_DIR}/${rel} COPYONLY)
endforeach()
