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

# Jolt Physics (cuerpos rígidos y ragdolls), como submódulo de git:
#   git submodule update --init
# Solo la biblioteca: sin tests, ejemplos, visor, perfilador ni render de depuración.
foreach(opt TARGET_UNIT_TESTS TARGET_HELLO_WORLD TARGET_PERFORMANCE_TEST TARGET_SAMPLES TARGET_VIEWER
        ENABLE_ALL_WARNINGS INTERPROCEDURAL_OPTIMIZATION GENERATE_DEBUG_SYMBOLS ENABLE_INSTALL
        DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE FLOATING_POINT_EXCEPTIONS_ENABLED
        JPH_USE_VK JPH_USE_DX12 JPH_USE_MTL)
    set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(third_party/JoltPhysics/Build EXCLUDE_FROM_ALL SYSTEM)  # SYSTEM: sin avisos de sus cabeceras

# Lógica pura (sin GL ni X11): la comparten el juego y los tests.
set(LOGIC_SOURCES
    src/skeleton.cpp
    src/character.cpp
    src/physics.cpp
    src/physics_jolt.cpp
    src/game.cpp
)

add_executable(game
    ${LOGIC_SOURCES}
    src/main.cpp
    src/platform_x11.cpp
    src/debug_draw.cpp
    src/game_draw.cpp
    src/text.cpp
    src/menu.cpp
    src/renderer.cpp
)

target_link_libraries(game PRIVATE Jolt OpenGL::OpenGL OpenGL::GLX X11::X11 X11::Xi)

target_compile_options(game PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<CONFIG:Debug>:-fsanitize=undefined -fno-sanitize=vptr -fno-omit-frame-pointer>
    $<$<BOOL:${GAME_NATIVE}>:-march=native>
)
target_link_options(game PRIVATE $<$<CONFIG:Debug>:-fsanitize=undefined>)
target_compile_definitions(game PRIVATE
    $<$<CONFIG:Debug>:GAME_DEBUG_GL=1 _GLIBCXX_ASSERTIONS>
)

# Tests de lógica: sin ventana, con AddressSanitizer + UBSan.
option(GAME_TESTS "Compilar los tests de lógica" ON)
if(GAME_TESTS)
    enable_testing()
    add_executable(logic_test tests/logic_test.cpp ${LOGIC_SOURCES})
    target_include_directories(logic_test PRIVATE src)
    target_link_libraries(logic_test PRIVATE Jolt)
    # vptr: Jolt se compila sin RTTI y UBSan no podría comprobar sus tipos dinámicos.
    target_compile_options(logic_test PRIVATE -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize=vptr
                           -fno-omit-frame-pointer)
    target_link_options(logic_test PRIVATE -fsanitize=address,undefined)
    add_test(NAME logic COMMAND logic_test)
endif()
