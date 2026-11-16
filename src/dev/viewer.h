#pragma once

// Visor de modelos .glb (--visor FICHERO.glb): enseña el modelo tal y como lo pinta el juego
// (mismo render, contorno, brillos y skinning) y lo anima con el motor: la protagonista y los
// humanoides con el mismo Character que en el juego, los enemigos con sus animaciones de IA.
// Se recarga solo al volver a exportar el fichero (o con R), para ir ajustando desde Blender.

#include "render/debug_draw.h"
#include "render/renderer.h"
#include "render/text.h"

// Abre el visor con el fichero `path` hasta cerrar la ventana o pulsar Esc. Entiende también
// --anim N (animación inicial), --noche, --cerca (la cara) y --screenshot FICHERO SEG [N] (guarda N fotogramas y sale).
int runViewer(const char* path, Renderer& renderer, DebugDraw& lines, UiLayer& ui, int argc, char** argv);
