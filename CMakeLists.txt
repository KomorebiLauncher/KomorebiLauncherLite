cmake_minimum_required(VERSION 3.21)
project(KomorebiNext LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network)

qt_standard_project_setup()

file(GLOB_RECURSE NEXT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
)

qt_add_executable(komorebi-next WIN32 ${NEXT_SOURCES})
target_link_libraries(komorebi-next PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network)
target_include_directories(komorebi-next PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The theme travels with the binary: tokens and the sheet are read at startup, not compiled in.
file(GLOB_RECURSE THEME_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/theme/*)
foreach(file ${THEME_FILES})
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${file})
    configure_file(${file} ${CMAKE_CURRENT_BINARY_DIR}/${rel} COPYONLY)
endforeach()
