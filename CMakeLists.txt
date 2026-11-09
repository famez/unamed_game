cmake_minimum_required(VERSION 3.16)
project(unamed_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug | Release | RelWithDebInfo" FORCE)
endif()

option(GAME_NATIVE "Optimizar para la CPU de esta máquina (-march=native)" ON)

# Sin dependencias externas: libOpenGL (glvnd) + GLX + Xlib, ya presentes en el sistema.
find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX)
find_package(X11 REQUIRED)

add_executable(game
    src/main.cpp
    src/platform_x11.cpp
    src/debug_draw.cpp
    src/skeleton.cpp
    src/character.cpp
    src/physics.cpp
    src/game.cpp
    src/game_draw.cpp
    src/text.cpp
    src/menu.cpp
    src/renderer.cpp
)

target_link_libraries(game PRIVATE OpenGL::OpenGL OpenGL::GLX X11::X11 X11::Xi)

target_compile_options(game PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<CONFIG:Debug>:-fsanitize=undefined -fno-omit-frame-pointer>
    $<$<BOOL:${GAME_NATIVE}>:-march=native>
)
target_link_options(game PRIVATE $<$<CONFIG:Debug>:-fsanitize=undefined>)
target_compile_definitions(game PRIVATE
    $<$<CONFIG:Debug>:GAME_DEBUG_GL=1 _GLIBCXX_ASSERTIONS>
)
