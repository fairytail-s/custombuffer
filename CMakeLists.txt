set(CMAKE_SKIP_BUILD_RPATH TRUE)
cmake_minimum_required(VERSION 3.22)
project(CustomBuffers VERSION 1.1.0 LANGUAGES CXX)

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

add_library(CustomBuffers SHARED
  src/main.cpp
  src/Mod.cpp
  src/GlHooks.cpp)

set_target_properties(CustomBuffers PROPERTIES PREFIX "" OUTPUT_NAME "CustomBuffers")
target_include_directories(CustomBuffers PRIVATE include vendor)
target_compile_options(CustomBuffers PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic)
target_link_options(CustomBuffers PRIVATE
  -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,-z,relro -Wl,-z,now -Wl,--build-id=sha1 -Wl,--no-undefined)

find_library(dl-lib dl REQUIRED)
target_link_libraries(CustomBuffers PRIVATE preloader ${dl-lib})
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET CustomBuffers POST_BUILD
  COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:CustomBuffers>"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_levipack.py"
    --library "$<TARGET_FILE:CustomBuffers>"
    --output "$<TARGET_FILE_DIR:CustomBuffers>/CustomBuffers.levipack"
  VERBATIM)
