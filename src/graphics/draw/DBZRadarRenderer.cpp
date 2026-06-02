#include "configuration.h"
#if HAS_SCREEN
#include "DBZRadarRenderer.h"
#include "NodeDB.h"
#include "gps/GeoCoord.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include <algorithm>
#include <cmath>

namespace graphics
{
namespace DBZRadarRenderer
{

// 0=Close (100m), 1=Med (1km), 2=Far (10km)
static uint8_t zoomLevel = 0;

void zoomIn()
{
    if (zoomLevel > 0)
        zoomLevel--;
}

void zoomOut()
{
    if (zoomLevel < 2)
        zoomLevel++;
}

// ---------------------------------------------------------------------------
// Geometry constants (all relative to frame x/y origin passed by the UI)
// ---------------------------------------------------------------------------
static constexpr int16_t RADAR_CX = 37;
static constexpr int16_t RADAR_CY = 32;
static constexpr int16_t RADAR_R  = 28; // 4 rings at 7, 14, 21, 28 px
static constexpr int16_t PANEL_X  = 70; // right info panel left edge

// ---------------------------------------------------------------------------
// Zoom tables
// ---------------------------------------------------------------------------
static const float       maxRanges[3]       = {100.0f, 1000.0f, 10000.0f};
static const char *const zoomNames[3]       = {"CLOSE", "MED", "FAR"};
static const char *const maxRangeLabels[3]  = {"100m",  "1km",  "10km"};
static const char *const ringLabels[3][4]   = {
    {"25m",  "50m",  "75m",  "100m"},
    {"250m", "500m", "750m", "1km" },
    {"2.5k", "5km",  "7.5k", "10km"},
};

// ---------------------------------------------------------------------------
// Live node record
// ---------------------------------------------------------------------------
static constexpr int MAX_RADAR_NODES = 12;

struct RadarNode {
    char  label;
    float distanceM;
    float bearingDeg;
};

// ---------------------------------------------------------------------------
// Helper: polar -> screen offset from radar center
// ---------------------------------------------------------------------------
static void bearingToXY(float bearingDeg, float fraction, int16_t &dx, int16_t &dy)
{
    if (fraction > 1.0f)
        fraction = 1.0f;
    float rad = bearingDeg * static_cast<float>(M_PI) / 180.0f;
    // North-up: bearing=0 -> tip up (-y), bearing=90 -> right (+x)
    dx = static_cast<int16_t>(fraction * RADAR_R * sinf(rad));
    dy = static_cast<int16_t>(-fraction * RADAR_R * cosf(rad));
}

// ---------------------------------------------------------------------------
// Core rendering helper
// ---------------------------------------------------------------------------
static void drawRingsAndPanel(OLEDDisplay *display, int16_t x, int16_t y)
{
    const int16_t cx = x + RADAR_CX;
    const int16_t cy = y + RADAR_CY;

    for (int ring = 1; ring <= 4; ring++) {
        display->drawCircle(cx, cy, (RADAR_R * ring) / 4);
    }

    // Outermost ring label (bottom-right corner of outer ring)
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(cx + RADAR_R + 2, cy + RADAR_R - FONT_HEIGHT_SMALL,
                        ringLabels[zoomLevel][3]);

    // Fixed north arrow at center (triangle pointing up)
    display->fillTriangle(cx, cy - 5, cx - 3, cy + 3, cx + 3, cy + 3);

    // Right info panel
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + PANEL_X, y + 0,  "DBZ v0.1");
    display->drawString(x + PANEL_X, y + 13, zoomNames[zoomLevel]);
    display->drawString(x + PANEL_X, y + 26, maxRangeLabels[zoomLevel]);
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void drawRadarFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    const int16_t cx = x + RADAR_CX;
    const int16_t cy = y + RADAR_CY;
    const float   maxRange = maxRanges[zoomLevel];

    drawRingsAndPanel(display, x, y);

    // --- Collect our own position ---
    const meshtastic_NodeInfoLite *ourNode = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!ourNode || !nodeDB->hasValidPosition(ourNode)) {
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(cx, cy + 10, "No GPS");
        return;
    }

    meshtastic_PositionLite ourPos;
    if (!nodeDB->copyNodePosition(ourNode->num, ourPos)) {
        return;
    }
    const double ourLat = ourPos.latitude_i * 1e-7;
    const double ourLon = ourPos.longitude_i * 1e-7;

    // --- Collect and sort up to 12 nearest nodes with valid positions ---
    RadarNode nodes[MAX_RADAR_NODES];
    int nodeCount = 0;

    size_t total = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < total; i++) {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
        if (!n || n->num == nodeDB->getNodeNum())
            continue;
        if (!nodeDB->hasValidPosition(n))
            continue;

        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(n->num, pos))
            continue;

        const double nodeLat = pos.latitude_i * 1e-7;
        const double nodeLon = pos.longitude_i * 1e-7;

        const float dist    = GeoCoord::latLongToMeter(ourLat, ourLon, nodeLat, nodeLon);
        const float bearing = GeoCoord::bearing(ourLat, ourLon, nodeLat, nodeLon);

        // Label: first char of short_name, or last decimal digit of node num
        char lbl = (n->short_name[0] != '\0') ? n->short_name[0]
                                               : static_cast<char>('0' + (n->num % 10));

        RadarNode entry = {lbl, dist, bearing};

        if (nodeCount < MAX_RADAR_NODES) {
            int pos2 = nodeCount++;
            nodes[pos2] = entry;
            while (pos2 > 0 && nodes[pos2].distanceM < nodes[pos2 - 1].distanceM) {
                RadarNode tmp    = nodes[pos2];
                nodes[pos2]     = nodes[pos2 - 1];
                nodes[pos2 - 1] = tmp;
                pos2--;
            }
        } else if (dist < nodes[MAX_RADAR_NODES - 1].distanceM) {
            nodes[MAX_RADAR_NODES - 1] = entry;
            int pos2 = MAX_RADAR_NODES - 1;
            while (pos2 > 0 && nodes[pos2].distanceM < nodes[pos2 - 1].distanceM) {
                RadarNode tmp    = nodes[pos2];
                nodes[pos2]     = nodes[pos2 - 1];
                nodes[pos2 - 1] = tmp;
                pos2--;
            }
        }
    }

    // --- Plot nodes ---
    for (int i = 0; i < nodeCount; i++) {
        float fraction = nodes[i].distanceM / maxRange;
        int16_t dx, dy;
        bearingToXY(nodes[i].bearingDeg, fraction, dx, dy);
        int16_t dotX = cx + dx;
        int16_t dotY = cy + dy;

        display->fillRect(dotX, dotY, 2, 2);

        char lbl[2] = {nodes[i].label, '\0'};
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(dotX, dotY + 3, lbl);
    }

    // Node count in panel
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    char cntBuf[8];
    snprintf(cntBuf, sizeof(cntBuf), "N:%d", nodeCount);
    display->drawString(x + PANEL_X, y + 39, cntBuf);

    // --- Serial log (throttled to once every 5 s) ---
    static uint32_t lastLogMs = 0;
    uint32_t now = millis();
    if (now - lastLogMs >= 5000) {
        lastLogMs = now;
        LOG_INFO("DBZ Radar zoom=%s maxRange=%.0fm ourLat=%.6f ourLon=%.6f nodes=%d",
                 zoomNames[zoomLevel], maxRange, ourLat, ourLon, nodeCount);
        for (int i = 0; i < nodeCount; i++) {
            float fraction = nodes[i].distanceM / maxRange;
            int16_t dx, dy;
            bearingToXY(nodes[i].bearingDeg, fraction, dx, dy);
            LOG_INFO("  node '%c' dist=%.1fm bear=%.1f dotX=%d dotY=%d%s",
                     nodes[i].label,
                     nodes[i].distanceM,
                     nodes[i].bearingDeg,
                     static_cast<int>(cx + dx),
                     static_cast<int>(cy + dy),
                     fraction >= 1.0f ? " [clamped]" : "");
        }
    }
}

} // namespace DBZRadarRenderer
} // namespace graphics
#endif // HAS_SCREEN
