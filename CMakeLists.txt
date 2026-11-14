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
find_package(Threads REQUIRED)  # el audio mezcla en su propio hilo

# Lógica pura (sin GL ni X11): la comparten el juego y los tests.
set(LOGIC_SOURCES
    src/skeleton.cpp
    src/character.cpp
    src/physics.cpp
    src/items.cpp
    src/enemy.cpp
    src/game.cpp
    src/game_story.cpp
    src/levels.cpp
    src/light.cpp
    src/test_mode.cpp
    src/shadow.cpp
    src/cinema.cpp
    src/brain.cpp
    src/allies.cpp
    src/story.cpp
    src/synth.cpp
)

add_executable(game
    ${LOGIC_SOURCES}
    src/main.cpp
    src/platform_x11.cpp
    src/debug_draw.cpp
    src/game_draw.cpp
    src/text.cpp
    src/menu.cpp
    src/inventory_menu.cpp
    src/models.cpp
    src/audio.cpp
    src/renderer.cpp
)

# ${CMAKE_DL_LIBS}: dlopen de PulseAudio en tiempo de ejecución (sin cabeceras al compilar).
target_link_libraries(game PRIVATE OpenGL::OpenGL OpenGL::GLX X11::X11 X11::Xi Threads::Threads ${CMAKE_DL_LIBS})

target_compile_options(game PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<CONFIG:Debug>:-fsanitize=undefined -fno-omit-frame-pointer>
    $<$<BOOL:${GAME_NATIVE}>:-march=native>
)
target_link_options(game PRIVATE $<$<CONFIG:Debug>:-fsanitize=undefined>)
target_compile_definitions(game PRIVATE
    GAME_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    $<$<CONFIG:Debug>:GAME_DEBUG_GL=1 _GLIBCXX_ASSERTIONS>
)

# Tests de lógica: sin ventana, con AddressSanitizer + UBSan.
option(GAME_TESTS "Compilar los tests de lógica" ON)
if(GAME_TESTS)
    enable_testing()
    add_executable(logic_test tests/logic_test.cpp ${LOGIC_SOURCES})
    target_include_directories(logic_test PRIVATE src)
    target_compile_definitions(logic_test PRIVATE GAME_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
    target_compile_options(logic_test PRIVATE -Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(logic_test PRIVATE -fsanitize=address,undefined)
    add_test(NAME logic COMMAND logic_test)
endif()
