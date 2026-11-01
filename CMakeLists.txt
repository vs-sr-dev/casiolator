cmake_minimum_required(VERSION 3.16)
project(casiolator VERSION 0.1.0 LANGUAGES C)

set(CL_VERSION_SUFFIX "-dev" CACHE STRING "Appended to the version number")
set(CL_VERSION "${PROJECT_VERSION}${CL_VERSION_SUFFIX}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(CL_SDL "Build the SDL2 frontend (window, sound, input)" ON)

add_executable(casiolator
  src/main.c src/game.c src/zip.c src/config.c src/os.c src/png.c src/font.c
  src/z80.c src/pv1000.c src/sh1.c src/sh7021.c src/loopy.c src/vdp.c src/upd937.c)
target_compile_definitions(casiolator PRIVATE CL_VERSION="${CL_VERSION}")
target_compile_options(casiolator PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(WIN32)
  target_link_libraries(casiolator PRIVATE comdlg32 shell32)
else()
  target_compile_definitions(casiolator PRIVATE _FILE_OFFSET_BITS=64)
  target_link_libraries(casiolator PRIVATE m)
endif()

if(CL_SDL)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 sdl2)
  endif()
  if(SDL2_FOUND)
    set_target_properties(casiolator PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(casiolator PRIVATE CL_HAVE_SDL SDL_MAIN_HANDLED)
    target_include_directories(casiolator PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_directories(casiolator PRIVATE ${SDL2_LIBRARY_DIRS})
    target_link_libraries(casiolator PRIVATE SDL2)
    if(WIN32 AND EXISTS "${SDL2_PREFIX}/bin/SDL2.dll")
      # so that the build folder runs with a double click
      add_custom_command(TARGET casiolator POST_BUILD COMMAND ${CMAKE_COMMAND} -E
        copy_if_different "${SDL2_PREFIX}/bin/SDL2.dll" "$<TARGET_FILE_DIR:casiolator>")
    endif()
  else()
    message(STATUS "SDL2 not found: headless-only build")
  endif()
endif()

add_executable(zextest tests/zextest.c src/z80.c)
add_executable(shdis tools/shdis.c src/sh1.c)
