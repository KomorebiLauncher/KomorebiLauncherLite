cmake_minimum_required(VERSION 3.21)
project(KomorebiNext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network LinguistTools)

qt_standard_project_setup()

# Headers are listed as well as sources: moc only runs for files the target knows about, and every
# page that declares signals lives in a subdirectory.
file(GLOB_RECURSE NEXT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

qt_add_executable(komorebi-lite WIN32 ${NEXT_SOURCES})

# Every header that declares a Q_OBJECT has to reach the target, or its moc output is missing.
file(GLOB_RECURSE NEXT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
target_sources(komorebi-lite PRIVATE ${NEXT_HEADERS})
set_target_properties(komorebi-lite PROPERTIES AUTOMOC ON)

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
if(WIN32)
    # CryptProtectData, for the account tokens
    target_link_libraries(komorebi-lite PRIVATE Crypt32)
endif()
target_include_directories(komorebi-lite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The theme travels with the binary: tokens and the sheet are read at startup, not compiled in.
file(GLOB_RECURSE THEME_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/theme/*)
foreach(file ${THEME_FILES})
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${file})
    configure_file(${file} ${CMAKE_CURRENT_BINARY_DIR}/${rel} COPYONLY)
endforeach()

# OptiFine ships no version metadata: its own installer is the only thing that knows how to patch the
# game, and it insists on a window. A small Java helper calls the same entry point without one. It is
# compiled next to the executable so a build always carries the class it was built from; a machine
# without a JDK still builds the launcher, and OptiFine then reports itself unavailable.
file(GLOB _jdk_hints
    "$ENV{JAVA_HOME}/bin"
    "$ENV{JDK_HOME}/bin"
    "C:/Program Files/Zulu/*/bin"
    "C:/Program Files/Eclipse Adoptium/*/bin"
    "C:/Program Files/Microsoft/jdk*/bin"
    "C:/Program Files/Java/*/bin"
)
find_program(JAVAC_EXECUTABLE NAMES javac HINTS ${_jdk_hints})
if(JAVAC_EXECUTABLE)
    set(OPTIFINE_RUNNER_DIR ${CMAKE_CURRENT_BINARY_DIR}/optifine-runner)
    set(OPTIFINE_RUNNER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tools/optifinerunner/komorebilauncher/optifine/Runner.java)
    add_custom_command(
        OUTPUT ${OPTIFINE_RUNNER_DIR}/komorebilauncher/optifine/Runner.class
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OPTIFINE_RUNNER_DIR}
        COMMAND ${JAVAC_EXECUTABLE} --release 8 -d ${OPTIFINE_RUNNER_DIR} ${OPTIFINE_RUNNER_SOURCE}
        DEPENDS ${OPTIFINE_RUNNER_SOURCE}
        COMMENT "Compiling the OptiFine runner"
        VERBATIM
    )
    add_custom_target(optifine-runner ALL DEPENDS ${OPTIFINE_RUNNER_DIR}/komorebilauncher/optifine/Runner.class)
    add_dependencies(komorebi-lite optifine-runner)
else()
    message(WARNING "javac was not found: installing OptiFine will report itself unavailable")
endif()
