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
    src/actors/skeleton.cpp
    src/actors/character.cpp
    src/actors/climb.cpp
    src/actors/hero_clips.cpp
    src/actors/physics.cpp
    src/game/items.cpp
    src/actors/enemy.cpp
    src/actors/creature.cpp
    src/game/creatures.cpp
    src/game/game.cpp
    src/game/combat.cpp
    src/game/hit_fx.cpp
    src/game/enemy_ai.cpp
    src/game/loot.cpp
    src/game/game_story.cpp
    src/game/levels.cpp
    src/game/levels_night.cpp
    src/game/levels_open.cpp
    src/game/levels_rio.cpp
    src/game/levels_copas.cpp
    src/game/levels_andamios.cpp
    src/game/npcs.cpp
    src/game/wild.cpp
    src/game/swarm.cpp
    src/game/light.cpp
    src/game/test_mode.cpp
    src/game/shadow.cpp
    src/game/cinema.cpp
    src/actors/brain.cpp
    src/game/allies.cpp
    src/game/story.cpp
    src/audio/synth.cpp
    src/game/props.cpp
    src/render/glb.cpp
)


# Shaders: ficheros .glsl/.vert/.frag de verdad, metidos en el binario como cadenas al compilar.
file(GLOB SHADER_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/shaders/*)
set(SHADERS_INC ${CMAKE_BINARY_DIR}/generated/shaders.inc)
add_custom_command(
    OUTPUT ${SHADERS_INC}
    COMMAND ${CMAKE_COMMAND} -DOUT=${SHADERS_INC} "-DSHADERS=${SHADER_FILES}" -P ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    DEPENDS ${SHADER_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_shaders.cmake
    COMMENT "Metiendo los shaders en el binario"
    VERBATIM)

add_executable(game
    ${SHADERS_INC}
    ${LOGIC_SOURCES}
    src/main.cpp
    src/core/platform_x11.cpp
    src/render/debug_draw.cpp
    src/game/game_draw.cpp
    src/render/text.cpp
    src/ui/menu.cpp
    src/ui/inventory_menu.cpp
    src/ui/hud.cpp
    src/ui/level_select.cpp
    src/dev/showcase.cpp
    src/dev/climb_bot.cpp
    src/dev/viewer.cpp
    src/render/glb_write.cpp
    src/render/hero_model.cpp
    src/render/models.cpp
    src/audio/audio.cpp
    src/render/renderer.cpp
    src/render/render_scene.cpp
    src/render/render_fx.cpp
    src/render/render_post.cpp
)

# ${CMAKE_DL_LIBS}: dlopen de PulseAudio en tiempo de ejecución (sin cabeceras al compilar).
target_include_directories(game PRIVATE src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(game PRIVATE OpenGL::OpenGL OpenGL::GLX X11::X11 X11::Xi Threads::Threads ${CMAKE_DL_LIBS})

target_compile_options(game PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    -g -fno-omit-frame-pointer  # símbolos para depurar cuelgues (no cambian el código generado)
    $<$<CONFIG:Debug>:-fsanitize=undefined -fno-omit-frame-pointer>
    $<$<BOOL:${GAME_NATIVE}>:-march=native>
)
target_link_options(game PRIVATE $<$<CONFIG:Debug>:-fsanitize=undefined> -rdynamic)  # -rdynamic: nombres en la pila de un cuelgue
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
