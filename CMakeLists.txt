set(CMAKE_SKIP_BUILD_RPATH TRUE)
cmake_minimum_required(VERSION 3.22)
project(AutoMiner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PRELOADER_LIB)
  add_library(preloader SHARED IMPORTED)
  set_target_properties(preloader PROPERTIES IMPORTED_LOCATION "${PRELOADER_LIB}")
else()
  message(STATUS "PRELOADER_LIB not set: building a link-only stub of libpreloader.so (do not ship it)")
  add_library(preloader SHARED stub/preloader_stub.cpp)
  target_include_directories(preloader PRIVATE vendor)
  set_target_properties(preloader PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stub")
endif()

add_library(AutoMiner SHARED
  src/main.cpp
  src/Mod.cpp
  src/Menu.cpp
  src/Runtime.cpp
  src/NativeBridge.cpp
  src/GlRenderer.cpp
  src/Controller.cpp
  src/Executor.cpp
  src/Planner.cpp
  src/Templates.cpp
  src/Settings.cpp
  src/Render.cpp)

set_target_properties(AutoMiner PROPERTIES PREFIX "" OUTPUT_NAME "AutoMiner")
target_include_directories(AutoMiner PRIVATE include vendor)
target_compile_options(AutoMiner PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic)
target_link_options(AutoMiner PRIVATE
  -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now -Wl,--build-id=sha1 -Wl,--no-undefined)

find_library(dl-lib dl REQUIRED)
target_link_libraries(AutoMiner PRIVATE preloader ${dl-lib} m)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET AutoMiner POST_BUILD
  COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:AutoMiner>"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_levipack.py"
    --library "$<TARGET_FILE:AutoMiner>"
    --output "$<TARGET_FILE_DIR:AutoMiner>/AutoMiner.levipack"
  VERBATIM)
