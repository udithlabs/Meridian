// Procedural, phase-accurate Moon sprite (CPU). Portable.
// Sprite frame: celestial north up, east to the left (as seen in the sky). Output is BGRA, premultiplied.
#pragma once
#include <vector>
#include <cstdint>

namespace moonsprite {
struct Params {
    int size = 256;
    double phaseAngleDeg = 90;     // 0 = full, 180 = new
    double brightLimbPA = 270;     // degrees from celestial north through east
    double earthshine = 0.06;      // 0..1 brightness of the unlit side
    float tint[3] = {1.0f, 0.985f, 0.94f};   // r, g, b multipliers
    double libLon = 0.0, libLat = 0.0;       // optical libration (degrees), optional
    double relief = 1.6;                     // strength of crater/mountain shading
    // Earth's shadow during a lunar eclipse. Centre offset and radii are in Moon radii; the offset is in sprite frame (x right, y up).
    bool shadow = false;
    double shX = 0, shY = 0, umbraR = 2.6, penumbraR = 4.7;
};
void render(const Params& p, std::vector<uint8_t>& bgra);
void prewarm();      // builds the lunar map on a background thread
bool ready();        // true once the map exists (render() would otherwise build it, blocking for ~0.5 s)
void shutdown();     // waits for prewarm() to finish before static destruction
}
