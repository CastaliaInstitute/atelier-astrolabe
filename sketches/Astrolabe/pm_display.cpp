#include "pm_display.h"

Arduino_Canvas *pm_gfx = nullptr;
void pm_display_bind(Arduino_Canvas *canvas) { pm_gfx = canvas; }
