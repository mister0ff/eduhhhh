// Overlay GL: hook em eglSwapBuffers; desenha outline + HUD/menu por cima do quadro.
#pragma once
namespace am {
bool installGlOverlay();
void removeGlOverlay();
}
