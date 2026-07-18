cmake_minimum_required(VERSION 3.19)

project(appimagetools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  list(APPEND COMPILE_OPTIONS -MP -W4 -wd4702)
else()
  list(APPEND COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wunused
    -Wshadow
    -Wundef
    -Wuninitialized
    -Wredundant-decls
    -Wcast-align
    -Winit-self
    -Wmissing-include-dirs
    -Wmissing-declarations
    -Wstrict-overflow=2
    -Wunused-parameter
    -Wformat=2
    -Wdisabled-optimization
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
  )
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND COMPILE_OPTIONS -Wno-unused-command-line-argument)
  endif()
endif()

add_compile_options(${COMPILE_OPTIONS})

add_definitions(
  -DBOOST_BIND_NO_PLACEHOLDERS
  -DQT_STRICT_ITERATORS
  -DQT_NO_CAST_FROM_BYTEARRAY
  -DQT_USE_QSTRINGBUILDER
  -DQT_NO_URL_CAST_FROM_STRING
  -DQT_NO_CAST_TO_ASCII
  -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT
  -DQT_NO_FOREACH
  -DQT_ASCII_CAST_WARNINGS
  -DQT_NO_CAST_FROM_ASCII
  -DQT_NO_KEYWORDS
  -DQT_NO_SIGNALS_SLOTS_KEYWORDS
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(LIEF REQUIRED CONFIG)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Svg)

qt_add_resources(APPIMAGEDEPLOY_RESOURCES data/data.qrc)

add_library(appimagetools_common OBJECT
  src/utilities.cpp
  src/elffile.cpp
  src/desktopentry.cpp
  src/logging.cpp
)

target_include_directories(appimagetools_common PUBLIC src)

target_link_libraries(appimagetools_common PUBLIC LIEF::LIEF Qt6::Core Qt6::Gui Qt6::Svg)

add_executable(appimagedeploy
  src/appimagedeploy_main.cpp
  src/appdir.cpp
  src/dependencywalker.cpp
  src/excludelist.cpp
  src/qtdeploy.cpp
  src/gstreamerdeploy.cpp
  src/miscdeploy.cpp
  src/appimagedeploy.cpp
  ${APPIMAGEDEPLOY_RESOURCES}
)

target_link_libraries(appimagedeploy PRIVATE Qt6::Core appimagetools_common)

add_executable(appimagebuilder
  src/appimagebuilder_main.cpp
  src/appimagebuilder.cpp
  src/appimagedeployoptions.h
)

target_link_libraries(appimagebuilder PRIVATE Qt6::Core appimagetools_common)

install(TARGETS appimagedeploy RUNTIME DESTINATION bin COMPONENT appimagedeploy)
install(TARGETS appimagebuilder RUNTIME DESTINATION bin COMPONENT appimagebuilder)

install(FILES dist/appimagedeploy.desktop DESTINATION share/applications COMPONENT appimagedeploy)
install(FILES dist/appimagebuilder.desktop DESTINATION share/applications COMPONENT appimagebuilder)

install(FILES dist/appimagetools.svg DESTINATION share/icons/hicolor/scalable/apps COMPONENT appimagedeploy)
install(FILES dist/appimagetools.svg DESTINATION share/icons/hicolor/scalable/apps COMPONENT appimagebuilder)
