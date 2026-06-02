#pragma once
#include "configuration.h"
#if HAS_SCREEN
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{
namespace DBZRadarRenderer
{

void drawRadarFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void zoomIn();
void zoomOut();

} // namespace DBZRadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
