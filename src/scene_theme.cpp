// Meridian: drawing theme packs (the layers of a pack's backdrop and extra tabs). See themes.h and themes/README.txt.
//
// The entry points are drawThemeBackdrop() and drawThemeFace(), called from App::render() (scene.cpp). Both do the same thing:
//   1. Frame::begin()   work out the values formulas can use (time, window size, weather light, countdowns ...)
//   2. for each layer   skip it if its "when" is false, then draw it with drawLayer()
//
// drawLayer() has one case per layer kind. To add a kind, see the note at the top of themes.h.
//
// Reading guide for the layer code: positions and sizes in a theme file are fractions of the window (x and w of the width,
// y and h of the height), so the same file fits any window shape. A formula may be used instead of any number.
#include "scene.h"
#include "themes.h"
#include <cmath>
#include <algorithm>

namespace app {

using namespace gfx;
using util::fmt;
using themes::Kind;
using themes::Layer;
using themes::Num;
using themes::Pack;

namespace {

constexpr float PI_F = 3.14159265f;

// Same idea as the built-in scenes: the same (i, salt) always gives the same number from 0 to 1.
float hashi(int i, int salt) {
    unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.f;
}
float phase(double t, double speed, double off = 0.0) { return (float)std::fmod(t * speed + off, 6.283185307179586); }

// ---------------------------------------------------------------- the values of one frame
struct Frame {
    Ctx& c;
    Pack& pk;
    float mul;                                   // fade of the whole scene (while a theme changes)
    int budget;                                  // pictures and cached drawings that may still be made this frame
    std::vector<double> v;                       // formula variables (slot order = BASE_VARS, countdown parts, then the pack's "vars")
    std::unordered_map<std::string, std::string> tok;    // the word {tokens}
    double t;

    Frame(Ctx& ctx, Pack& p, float m) : c(ctx), pk(p), mul(m), budget(std::max(1, ctx.a.spriteBudget)), t(ctx.a.now) { begin(); }

    void begin() {
        App& a = c.a;
        const util::LocalTime L = util::toLocal(a.now, a.tzOff);
        const double hour = L.hh + L.mm / 60.0 + (L.ss + L.fracSec) / 3600.0;
        const float light = (1.f - 0.45f * a.gloom) * (1.f - 0.55f * a.haze);
        v.assign(std::max<size_t>(pk.vars.names.size(), 32), 0.0);
        // BASE_VARS order (themes.cpp): t W H s portrait gloom haze hour sunAlt weekday day month year minute second light tzHours
        const double base[] = {t, (double)c.W, (double)c.H, (double)c.s, c.W < c.H * 1.15f ? 1.0 : 0.0, (double)a.gloom, (double)a.haze, hour, a.sun.hor.alt,
                               (double)L.wday, (double)L.d, (double)L.m, (double)L.y, (double)L.mm, (double)L.ss, (double)light, a.tzOff / 3600.0};
        for (size_t i = 0; i < sizeof base / sizeof base[0]; ++i) v[i] = base[i];
        // countdowns: left past days hrs min sec done daysf
        for (const themes::Countdown& cd : pk.countdowns) {
            if (cd.firstVar < 0 || (size_t)cd.firstVar + 8 > v.size()) continue;
            const double at = pk.countdownAt(cd, a.now, a.tzOff), rem = at - a.now, ar = std::fabs(rem);
            double* d = &v[(size_t)cd.firstVar];
            d[0] = std::max(0.0, rem); d[1] = std::max(0.0, -rem);
            d[2] = std::floor(ar / 86400.0); d[3] = std::floor(std::fmod(ar, 86400.0) / 3600.0); d[4] = std::floor(std::fmod(ar, 3600.0) / 60.0); d[5] = std::floor(std::fmod(ar, 60.0));
            d[6] = rem <= 0 ? 1.0 : 0.0; d[7] = std::max(0.0, rem) / 86400.0;
            const util::LocalTime R = util::toLocal(at, a.tzOff);
            tok[cd.id + "_when"] = fmt("%s %d %s %d", util::WEEKDAYS[R.wday], R.d, util::MONTHS[R.m - 1], R.y);
            tok[cd.id + "_clock"] = util::fmtClock(at, a.tzOff, a.st.h24);
            tok[cd.id + "_date"] = fmt("%d %s", R.d, util::MONTHS[R.m - 1]);
            tok[cd.id + "_short"] = fmt("%d %s", R.d, util::MONTHS3[R.m - 1]);
            tok[cd.id + "_dow"] = util::WEEKDAYS3[R.wday];
        }
        // numbers set by clicks (kept in App::packState), and numbers that follow the pointer (their start value now; hoverPass() sets them)
        for (const themes::StateVar& sv : pk.states) {
            if (sv.hover) { v[(size_t)sv.slot] = sv.def; continue; }
            const auto it = a.packState.find(pk.id + "/" + sv.name);
            v[(size_t)sv.slot] = it == a.packState.end() ? sv.def : it->second.first;
            v[(size_t)sv.slotT] = it == a.packState.end() ? 3600.0 : std::max(0.0, std::min(3600.0, a.now - it->second.second));
        }
        userVars();
        { const int off = a.tzOff; tok["utc"] = off % 3600 == 0 ? fmt("UTC%+d", off / 3600) : fmt("UTC%+d:%02d", off / 3600, std::abs(off % 3600) / 60); }
        // words
        int h12 = L.hh % 12; if (h12 == 0) h12 = 12;
        tok["hm"] = a.st.h24 ? fmt("%02d:%02d", L.hh, L.mm) : fmt("%d:%02d", h12, L.mm);
        tok["ampm"] = a.st.h24 ? "" : (L.hh < 12 ? "AM" : "PM");
        tok["time"] = a.st.h24 ? tok["hm"] : tok["hm"] + " " + tok["ampm"];
        tok["sec"] = fmt("%02d", L.ss);
        tok["weekday"] = util::WEEKDAYS[L.wday]; tok["weekdayShort"] = util::WEEKDAYS3[L.wday];
        tok["day"] = fmt("%d", L.d); tok["monthname"] = util::MONTHS[L.m - 1]; tok["mon"] = util::MONTHS3[L.m - 1];
        tok["year"] = fmt("%d", L.y); tok["date"] = fmt("%d %s", L.d, util::MONTHS[L.m - 1]);
    }

    void userVars() { for (auto& uv : pk.userVars) v[(size_t)uv.first] = uv.second->eval(v.data()); }       // the pack's own variables, in the order written

    // The pointer is over a button that names a "hover" variable: give the variable its value (before anything is drawn, so no frame is half and half)
    void hoverPass(const themes::Scene& sc) {
        App& a = c.a;
        if (a.showSettings || a.showHelp || a.isSaver || a.mouseX < 0 || a.mouseY < 0) return;
        bool any = false;
        const float mx = a.mouseX - a.shiftX, my = a.mouseY - a.shiftY;
        for (const Layer& L : sc.layers) {
            if (L.kind != Kind::Button || L.slotHover < 0 || !on(L)) continue;
            const float x = num(L.x) * c.W, y = num(L.y) * c.H, w = num(L.w) * c.W, h = num(L.h) * c.H;
            if (mx >= x && mx < x + w && my >= y && my < y + h) { v[(size_t)L.slotHover] = num(L.hoverTo, 1.f); any = true; }
        }
        if (any) userVars();
    }

    float num(const Num& n, float def = 0.f) const { return (float)n.eval(v.data(), def); }
    bool on(const Layer& L) const { return !L.when.set || L.when.eval(v.data()) != 0.0; }

    // a colour of the layer: a fixed one, or one of the live palette
    Color color(const themes::Col& col, const Color& def) const {
        if (!col.set) return def;
        const Palette& P = c.P;
        Color b = col.c;
        switch (col.src) {
            case themes::Col::Ink: b = P.ink; break;       case themes::Col::Ink2: b = P.ink2; break;     case themes::Col::Ink3: b = P.ink3; break;
            case themes::Col::Accent: b = P.accent; break; case themes::Col::Accent2: b = P.accent2; break;
            case themes::Col::Line: b = P.line; break;     case themes::Col::Panel: b = P.panel; break;   case themes::Col::Plate: b = P.plate; break;
            default: return col.c;
        }
        b.a *= col.c.a;
        return b;
    }

    std::string expand(const themes::Tpl& t) const {
        std::string out;
        for (const themes::TextPiece& p : t.pieces) {
            if (p.type == themes::TextPiece::Lit) out += p.s;
            else if (p.type == themes::TextPiece::Var) out += fmt("%0*lld", p.width, (long long)std::floor(v[(size_t)p.var] + 1e-9));
            else { auto it = tok.find(p.s); if (it != tok.end()) out += it->second; }
        }
        return out;
    }
    // the text a Choice shows right now ("" when no case applies); 'cycle' picks among several texts of the case
    std::string pick(const themes::Choice& ch, long long cycle) const {
        for (const themes::TextCase& tc : ch.cases) {
            if (tc.when.set && tc.when.eval(v.data()) == 0.0) continue;
            if (tc.texts.empty()) continue;
            const long long n = (long long)tc.texts.size();
            return expand(tc.texts[(size_t)((cycle % n + n) % n)]);
        }
        return "";
    }
};

FontId fontOf(const std::string& s, FontId def) {
    static const char* names[F_COUNT] = {"display", "light", "medium", "serif", "serifItalic", "mono", "monoMedium"};     // same order as FontId
    for (int i = 0; i < F_COUNT; ++i) if (s == names[i]) return (FontId)i;
    return def;
}

// "bottom-center", "top left", "right" ... -> where the picture's anchor point is within it (0..1, 0..1)
void anchorOf(const std::string& s, float& ax, float& ay) {
    ax = 0.5f; ay = 0.5f;
    if (s.find("left") != std::string::npos) ax = 0.f;
    if (s.find("right") != std::string::npos) ax = 1.f;
    if (s.find("top") != std::string::npos) ay = 0.f;
    if (s.find("bottom") != std::string::npos) ay = 1.f;
}

void starShape(Gfx& g, float cx, float cy, float r, const Color& fill, const Color& edge, float edgeW, bool lit, int sides) {
    std::vector<Pt> p;
    for (int i = 0; i < sides * 2; ++i) {
        const float a = -PI_F * 0.5f + i * PI_F / sides, rr = (i & 1) ? r * 0.42f : r;
        p.push_back({cx + std::cos(a) * rr, cy + std::sin(a) * rr});
    }
    if (lit) g.fillPolygon(p, fill);
    g.polyline(p, true, edge, edgeW, true);
}

// ---------------------------------------------------------------- the skyline (drawn once into a picture, then only placed)
void buildSkyline(Gfx& g, const Frame& f, const Layer& L, float sw, float sh) {
    const std::vector<themes::Col>& cs = L.colors;
    const Color far_ = cs.size() > 0 ? f.color(cs[0], Color{0.23f, 0.08f, 0.35f, 0.92f}) : Color{0.23f, 0.08f, 0.35f, 0.92f};
    const Color near_ = cs.size() > 1 ? f.color(cs[1], Color{0.10f, 0.03f, 0.20f, 1.f}) : Color{0.10f, 0.03f, 0.20f, 1.f};
    const Color w1 = L.windows.size() > 0 ? f.color(L.windows[0], Color{1.f, 0.43f, 0.70f, 0.85f}) : Color{1.f, 0.43f, 0.70f, 0.85f};
    const Color w2 = L.windows.size() > 1 ? f.color(L.windows[1], Color{0.2f, 0.9f, 0.83f, 0.8f}) : w1;
    const float sides = f.num(L.sides, 1.f);
    const int seed = (int)f.num(L.seed, 0.f) * 977;
    for (int layer = 0; layer < 2; ++layer) {
        const Color body = layer == 0 ? far_ : near_;
        float x = -0.01f * sw; int i = 0;
        while (x < sw) {
            const int k = i + layer * 100 + seed;
            const float bw = sw * (0.012f + 0.022f * hashi(k, 21));
            const float edge = std::fabs((x / sw) - 0.5f) * 2.f;                                   // taller toward the sides, so the middle stays clear
            const float tall = (1.f - sides) + sides * (0.38f + 0.75f * edge);
            const float bh = std::min(sh, sh * (0.22f + 0.55f * hashi(k, 22)) * tall * (layer == 0 ? 1.f : 0.62f));
            g.fillRect(x, sh - bh, bw + 1.f, bh, body);
            if (layer == 0 && bh > sh * 0.18f) {                                                    // a few lit windows
                const int nw = (int)(bh / (sh * 0.07f));
                for (int w = 0; w < nw; ++w) if (hashi(k * 31 + w, 23) > 0.72f) {
                    const Color wc = hashi(k * 31 + w, 24) > 0.5f ? w1 : w2;
                    g.fillRect(x + bw * (0.2f + 0.45f * hashi(k * 31 + w, 25)), sh - bh + (w + 0.6f) * sh * 0.07f, std::max(1.5f, bw * 0.16f), std::max(1.5f, sh * 0.02f), wc);
                }
            }
            x += bw + sw * 0.002f * hashi(k, 26); ++i;
        }
    }
}

// ---------------------------------------------------------------- map layer: countries drawn into a picture (made once, then placed with the layer's opacity)
float smooth01(float a, float b, float x) { const float u = clamp01((x - a) / (b - a)); return u * u * (3.f - 2.f * u); }
uint64_t mix64(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h * 0x100000001B3ull; }
uint64_t colKey(const Color& k) { return ((uint64_t)(k.r * 255.f + 0.5f) << 24) | ((uint64_t)(k.g * 255.f + 0.5f) << 16) | ((uint64_t)(k.b * 255.f + 0.5f) << 8) | (uint64_t)(k.a * 255.f + 0.5f); }

bool inGroups(const Layer& L, const themes::MapData::Group& gr) {
    for (const std::string& n : L.groups) {
        if (n == "*") { if (gr.id.empty() || gr.id[0] != '_') return true; }      // "*": every country (the helper groups, named _something, only when asked for)
        else if (n == gr.id) return true;
    }
    return false;
}

void drawMapLayer(Frame& f, const Layer& L, float op) {
    Ctx& c = f.c; Gfx& g = c.g;
    const themes::MapData* md = themes::mapData(f.pk, L.src);
    if (!md) return;
    const float W = c.W, H = c.H;
    const float bw = std::max(8.f, f.num(L.w, 0.9f) * W), bh = bw / (float)md->aspect, bx = f.num(L.x, 0.05f) * W, by = f.num(L.y, 0.2f) * H;
    const Color fill = f.color(L.color, Color{0.f, 0.f, 0.f, 0.f}), line = f.color(L.color2, Color{0.f, 0.f, 0.f, 0.f});
    const float thick = L.thick.set ? std::max(0.6f, f.num(L.thick) * H) : 0.f, glowW = f.num(L.glow) * H;
    std::vector<const themes::MapData::Group*> gs;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const themes::MapData::Group& gr : md->groups) if (inGroups(L, gr)) { gs.push_back(&gr); x0 = std::min(x0, gr.x0); y0 = std::min(y0, gr.y0); x1 = std::max(x1, gr.x1); y1 = std::max(y1, gr.y1); }
    if (gs.empty()) return;
    const float m = std::ceil(std::max(glowW, 0.f) + thick * 2.f + 3.f);
    const int sw = (int)std::ceil((x1 - x0) * bw + 2.f * m), sh = (int)std::ceil((y1 - y0) * bh + 2.f * m);
    if (sw < 2 || sh < 2 || (long long)sw * sh > 20000000ll) return;
    uint64_t key = 1469598103934665603ull;
    key = mix64(key, (uint64_t)(bw * 2.f)); key = mix64(key, (uint64_t)(bh * 2.f)); key = mix64(key, (uint64_t)(thick * 10.f)); key = mix64(key, (uint64_t)(glowW * 10.f));
    key = mix64(key, colKey(fill)); key = mix64(key, colKey(line)); key = mix64(key, gs.size());
    if (!L.sprite.bmp || L.spriteKey != key) {
        if (f.budget <= 0) { if (!L.sprite.bmp) return; }
        else {
            --f.budget; g.freeBitmap(L.sprite);
            if (g.beginSprite(sw, sh)) {
                const float ox = -x0 * bw + m, oy = -y0 * bh + m;
                std::vector<Pt> pts;
                auto ring = [&](const std::vector<Pt>& r) { pts.resize(r.size()); for (size_t i = 0; i < r.size(); ++i) pts[i] = {r[i].x * bw + ox, r[i].y * bh + oy}; };
                const Color halo = line.a > 0.f ? line : fill;
                if (glowW > 0.5f && halo.a > 0.f) {                                    // the glow: soft round lights all along the coast (wide strokes would spike at sharp corners)
                    const float step = std::max(2.f, glowW * 0.4f);
                    for (const auto* gr : gs) if (!gr->open) for (const auto& r : gr->rings) {
                        ring(r);
                        const size_t np = pts.size();
                        for (size_t i = 0; i < np; ++i) {
                            const Pt a = pts[i], b = pts[(i + 1) % np];
                            const float len = std::hypot(b.x - a.x, b.y - a.y);
                            const int k = std::max(1, (int)std::ceil(len / step));
                            for (int j = 0; j < k; ++j) { const float u = (float)j / (float)k; g.glow(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, glowW, withA(halo, 1.f), 0.085f * halo.a); }
                        }
                    }
                }
                if (fill.a > 0.f) for (const auto* gr : gs) if (!gr->open) for (const auto& r : gr->rings) { ring(r); if (pts.size() >= 3) g.fillPolygon(pts, fill); }
                if (line.a > 0.f && thick > 0.f) for (const auto* gr : gs) for (const auto& r : gr->rings) {     // outlines segment by segment: round ends, and no spiked corners
                    ring(r);
                    const size_t np = pts.size(), last = gr->open ? np - 1 : np;
                    for (size_t i = 0; i < last; ++i) g.line(pts[i].x, pts[i].y, pts[(i + 1) % np].x, pts[(i + 1) % np].y, line, thick, true);
                }
                L.sprite = g.endSprite(); L.spriteKey = key; ++gSpriteBuilds;
                L.spriteX = x0 * bw - m; L.spriteY = y0 * bh - m;
            }
            if (!L.sprite.bmp) return;
        }
    }
    g.drawBitmapRect(L.sprite, bx + L.spriteX, by + L.spriteY, (float)L.sprite.w, (float)L.sprite.h, op);
}

// ---------------------------------------------------------------- traffic layer: jets with trails, boats with wakes, a police helicopter
// A trip is a pure function of the time, so nothing is stored: where a plane was a moment ago (for its trail) is just the same formula at an earlier time.
void jetShape(Gfx& g, float x, float y, float len, float dir, const Color& body, const Color& wing) {
    auto P = [&](float u, float v) { return Pt{x + u * len * dir, y + v * len}; };
    g.fillPolygon({P(0.10f, 0.03f), P(-0.14f, 0.46f), P(-0.27f, 0.46f), P(-0.10f, 0.03f)}, wing);
    g.fillPolygon({P(0.10f, -0.03f), P(-0.14f, -0.46f), P(-0.27f, -0.46f), P(-0.10f, -0.03f)}, wing);
    g.fillPolygon({P(-0.38f, 0.02f), P(-0.50f, 0.18f), P(-0.59f, 0.18f), P(-0.50f, 0.02f)}, wing);
    g.fillPolygon({P(-0.38f, -0.02f), P(-0.50f, -0.18f), P(-0.59f, -0.18f), P(-0.50f, -0.02f)}, wing);
    g.fillPolygon({P(0.52f, 0.f), P(0.44f, -0.04f), P(-0.40f, -0.032f), P(-0.52f, -0.012f), P(-0.52f, 0.012f), P(-0.40f, 0.032f), P(0.44f, 0.04f)}, body);
}

void drawJets(Frame& f, const Layer& L, float op) {
    Ctx& c = f.c; Gfx& g = c.g;
    const float W = c.W, H = c.H;
    const double t = f.t;
    const int n = (int)std::max(0.f, std::min(12.f, f.num(L.count, 3.f)));
    const int seed = (int)f.num(L.seed, 0.f) * 1013;
    const float y0 = f.num(L.y0, 0.08f) * H, y1 = f.num(L.y1, 0.34f) * H, sz = f.num(L.size, 0.02f) * H;
    const double period = std::max(20.f, f.num(L.period, 150.f));
    const float act = std::max(0.2f, std::min(1.f, f.num(L.active, 0.62f))), fadeAt = std::max(0.1f, std::min(0.95f, f.num(L.fade, 0.6f)));
    const float trailSec = std::max(2.f, f.num(L.trail, 22.f)), dirSet = f.num(L.dir, 0.f), speed = std::max(0.2f, f.num(L.speed, 1.f));
    const Color body = f.color(L.color, Color{1.f, 0.95f, 0.92f, 1.f}), trailc = f.color(L.color2, Color{1.f, 0.90f, 0.95f, 1.f});
    const int N = 16;
    for (int i = 0; i < n; ++i) {
        const double P = period / speed * (0.8 + 0.5 * hashi(i + seed, 71));
        const double tt = t + hashi(i + seed, 72) * P * 5.0;
        const long long cyc = (long long)std::floor(tt / P);
        const double u = tt / P - (double)cyc;
        const int k = (int)(cyc % 100003) * 31 + i * 7 + seed;
        const float dirv = dirSet != 0.f ? (dirSet > 0.f ? 1.f : -1.f) : (hashi(k, 73) > 0.5f ? 1.f : -1.f);
        const float ly = y0 + (y1 - y0) * hashi(k, 74), szk = sz * (0.8f + 0.45f * hashi(k, 75)), climb = (0.02f + 0.05f * hashi(k, 76)) * H;
        const double trailFrac = (double)trailSec / (P * act), sNow = u / act;
        if (sNow > 1.0 + trailFrac + 0.01) continue;
        const float span = 0.98f * W;
        auto pos = [&](double sv, float& px, float& py) { px = dirv > 0.f ? -0.04f * W + (float)sv * span : 1.04f * W - (float)sv * span; py = ly - (float)sv * climb; };
        auto env = [&](double sv) { return smooth01(0.f, 0.08f, (float)sv) * (1.f - smooth01(fadeAt, 1.f, (float)sv)); };
        float px, py; pos(sNow, px, py);
        const float tailx = dirv * 0.5f * szk;
        float qx = px - tailx, qy = py;                                         // where the trail starts: the plane's tail
        const float thin = std::max(1.f, 0.0011f * H);
        for (int j = 1; j <= N; ++j) {
            const double sv = sNow - (double)j * trailFrac / N;
            if (sv < 0.0) break;
            float rx, ry; pos(sv, rx, ry); rx -= tailx;
            const float kk = (float)j / N, a = env(sv) * std::pow(1.f - kk, 1.3f) * 0.62f * op;
            if (a > 0.01f) {
                const float o0 = (0.06f + 0.30f * (float)(j - 1) / N) * szk, o1 = (0.06f + 0.30f * kk) * szk;
                g.line(qx, qy, rx, ry, withA(trailc, trailc.a * a * 0.22f), thin * (4.f + 0.8f * (o1 / szk) * 30.f), false);      // the soft haze around the trail (one wide line)
                for (int sg = -1; sg <= 1; sg += 2)
                    g.line(qx, qy + sg * o0, rx, ry + sg * o1, withA(trailc, trailc.a * a), thin * (1.f + 0.6f * kk), false);
            }
            qx = rx; qy = ry;
        }
        const float e = env(sNow) * op;
        if (e > 0.02f) {
            g.glow(px, py, szk * 1.6f, withA(body, 1.f), 0.16f * e);
            jetShape(g, px, py, szk, dirv, withA(body, body.a * e), withA(lerp(body, trailc, 0.45f), body.a * e * 0.9f));
            const float lr = std::max(1.f, 0.07f * szk);
            g.fillCircle(px - dirv * 0.26f * szk, py - 0.46f * szk, lr, withA(Color{1.f, 0.2f, 0.25f, 1.f}, 0.9f * e));      // wing-tip lights
            g.fillCircle(px - dirv * 0.26f * szk, py + 0.46f * szk, lr, withA(Color{0.2f, 1.f, 0.55f, 1.f}, 0.9f * e));
            if (std::fmod(t * 0.8 + hashi(k, 77), 1.0) < 0.07) g.fillCircle(px - dirv * 0.55f * szk, py, lr * 1.8f, withA(Color{1.f, 1.f, 1.f, 1.f}, e));    // the flash
        }
    }
}

void drawBoats(Frame& f, const Layer& L, float op) {
    Ctx& c = f.c; Gfx& g = c.g;
    const float W = c.W, H = c.H;
    const double t = f.t;
    const int n = (int)std::max(0.f, std::min(12.f, f.num(L.count, 4.f)));
    const int seed = (int)f.num(L.seed, 0.f) * 1013;
    const float y0 = f.num(L.y0, 0.84f) * H, y1 = f.num(L.y1, 0.97f) * H, sz = f.num(L.size, 0.11f) * H;
    const double period = std::max(10.f, f.num(L.period, 90.f));
    const float dirSet = f.num(L.dir, 0.f), speed = std::max(0.2f, f.num(L.speed, 1.f));
    const Color hull = f.color(L.color, Color{0.08f, 0.02f, 0.16f, 1.f}), lights = f.color(L.color2, Color{1.f, 0.43f, 0.70f, 1.f}), white = f.color(L.color3, Color{1.f, 0.93f, 0.95f, 1.f});
    for (int i = 0; i < n; ++i) {
        const float depth = (i + 0.25f + 0.5f * hashi(i + seed, 81)) / (float)n;               // 0 at the far lane, 1 at the near one
        const float sc = 0.30f + 0.70f * depth, y = y0 + (y1 - y0) * depth;
        const double P = period / speed * (1.5 - 0.95 * depth) * (0.85 + 0.3 * hashi(i + seed, 82));
        const double tt = t + hashi(i + seed, 83) * P * 7.0;
        const long long cyc = (long long)std::floor(tt / P);
        const double u = tt / P - (double)cyc;
        const int k = (int)(cyc % 100003) * 17 + i * 5 + seed;
        const float dirv = dirSet != 0.f ? (dirSet > 0.f ? 1.f : -1.f) : (hashi(k, 84) > 0.5f ? 1.f : -1.f);
        const float r = hashi(k, 85);
        int type;                                                                              // 0 sail  1 yacht  2 speedboat  3 ferry  4 jet ski
        if (depth < 0.38f) type = r < 0.55f ? 3 : 0; else type = r < 0.28f ? 0 : r < 0.62f ? 1 : r < 0.88f ? 2 : 4;
        float len = sz * sc * (0.85f + 0.3f * hashi(k, 86));
        if (type == 3) len *= 1.7f; else if (type == 4) len *= 0.42f; else if (type == 2) len *= 0.7f;
        const float x = dirv > 0.f ? -len * 1.5f + (float)u * (W + len * 3.f) : W + len * 1.5f - (float)u * (W + len * 3.f);
        const float wy = y + std::sin(phase(t, 1.1 + 0.2 * hashi(k, 87), i)) * 0.0013f * H * sc;
        const float al = op * (0.58f + 0.42f * depth);
        auto P_ = [&](float uu, float vv) { return Pt{x + uu * len * dirv, wy + vv * len}; };
        const Color body = withA(hull, hull.a * al), wh = withA(white, white.a * al), li = withA(lights, lights.a * al);
        const float thin = std::max(1.f, 0.012f * len);
        // the wake: a flat V of foam behind the stern, fading
        {
            const float wl = len * (type == 2 || type == 4 ? 2.6f : type == 3 ? 1.2f : 1.5f), wa = (type == 2 || type == 4 ? 0.55f : 0.34f) * al;
            const float stx = x - dirv * 0.5f * len; float qx = stx, qyu = wy, qyd = wy;
            for (int j = 1; j <= 12; ++j) {
                const float kk = j / 12.f, rx = stx - dirv * kk * wl, off = std::pow(kk, 0.9f) * 0.075f * len;
                const float a = wa * std::pow(1.f - kk, 1.4f);
                g.line(qx, qyu, rx, wy - off, withA(white, white.a * a), thin * (1.4f - kk), false);
                g.line(qx, qyd, rx, wy + off * 0.8f, withA(white, white.a * a), thin * (1.4f - kk), false);
                qx = rx; qyu = wy - off; qyd = wy + off * 0.8f;
            }
            if (type == 2 || type == 4) g.glowEllipse(stx - dirv * 0.15f * len, wy, len * 0.28f, len * 0.05f, withA(white, 1.f), 0.45f * al);
        }
        // the reflection of the lights in the water
        {
            const float rw = 0.40f * len;
            std::vector<Pt> rf = {P_(-0.5f, 0.03f), P_(0.5f, 0.03f), P_(0.5f * (rw / len) * 1.6f, 0.55f), P_(-0.5f * (rw / len) * 1.6f, 0.55f)};
            g.fillPolyVGradient(rf, wy, wy + 0.55f * len, withA(lights, 0.20f * al), withA(lights, 0.f));
        }
        if (type == 0) {
            g.fillPolygon({P_(0.50f, -0.07f), P_(0.36f, 0.02f), P_(-0.40f, 0.02f), P_(-0.50f, -0.08f)}, body);
            g.line(P_(0.02f, -0.07f).x, P_(0.02f, -0.07f).y, P_(0.02f, -0.92f).x, P_(0.02f, -0.92f).y, body, thin, false);
            g.fillPolygon({P_(-0.02f, -0.12f), P_(-0.02f, -0.88f), P_(-0.44f, -0.14f)}, withA(white, white.a * al * 0.92f));
            g.fillPolygon({P_(0.06f, -0.12f), P_(0.06f, -0.74f), P_(0.47f, -0.10f)}, withA(lerp(white, lights, 0.4f), white.a * al * 0.85f));
            g.line(P_(-0.02f, -0.88f).x, P_(-0.02f, -0.88f).y, P_(-0.44f, -0.14f).x, P_(-0.44f, -0.14f).y, li, thin * 0.8f, false);
        } else if (type == 1) {
            g.fillPolygon({P_(0.52f, -0.06f), P_(0.44f, 0.03f), P_(-0.50f, 0.03f), P_(-0.52f, -0.10f)}, body);
            g.line(P_(0.50f, -0.055f).x, P_(0.50f, -0.055f).y, P_(-0.50f, -0.09f).x, P_(-0.50f, -0.09f).y, withA(white, white.a * al * 0.85f), thin * 1.1f, false);
            g.fillPolygon({P_(0.30f, -0.06f), P_(0.34f, -0.14f), P_(-0.34f, -0.16f), P_(-0.40f, -0.09f)}, wh);
            g.fillPolygon({P_(0.14f, -0.14f), P_(0.18f, -0.22f), P_(-0.26f, -0.23f), P_(-0.30f, -0.15f)}, withA(lerp(white, lights, 0.15f), white.a * al));
            g.fillPolygon({P_(0.10f, -0.22f), P_(0.12f, -0.29f), P_(-0.08f, -0.30f), P_(-0.10f, -0.23f)}, body);
            g.line(P_(-0.05f, -0.30f).x, P_(-0.05f, -0.30f).y, P_(-0.05f, -0.44f).x, P_(-0.05f, -0.44f).y, body, thin * 0.8f, false);
            for (int w = 0; w < 6; ++w) { const Pt q = P_(0.24f - w * 0.10f, -0.125f); g.fillRect(q.x - 0.012f * len, q.y - 0.012f * len, std::max(1.5f, 0.026f * len), std::max(1.5f, 0.022f * len), li); }
            if (std::sin(phase(t, 4.0, i)) > 0.2f) { const Pt q = P_(-0.05f, -0.45f); g.fillCircle(q.x, q.y, std::max(1.f, 0.014f * len), withA(Color{1.f, 0.25f, 0.3f, 1.f}, al)); }
        } else if (type == 2) {
            g.fillPolygon({P_(0.50f, -0.12f), P_(0.30f, 0.03f), P_(-0.50f, 0.03f), P_(-0.50f, -0.08f)}, body);
            g.fillPolygon({P_(0.12f, -0.11f), P_(0.03f, -0.24f), P_(-0.08f, -0.24f), P_(-0.10f, -0.10f)}, withA(lerp(hull, lights, 0.5f), al));
            g.line(P_(0.46f, -0.10f).x, P_(0.46f, -0.10f).y, P_(-0.48f, -0.075f).x, P_(-0.48f, -0.075f).y, li, thin, false);
        } else if (type == 3) {
            g.fillPolygon({P_(0.50f, -0.04f), P_(0.46f, 0.04f), P_(-0.50f, 0.04f), P_(-0.50f, -0.11f)}, body);
            g.fillPolygon({P_(0.40f, -0.11f), P_(0.38f, -0.20f), P_(-0.42f, -0.20f), P_(-0.42f, -0.11f)}, body);
            g.fillPolygon({P_(0.30f, -0.20f), P_(0.28f, -0.28f), P_(-0.34f, -0.28f), P_(-0.34f, -0.20f)}, body);
            g.fillPolygon({P_(0.16f, -0.28f), P_(0.14f, -0.35f), P_(-0.20f, -0.35f), P_(-0.20f, -0.28f)}, body);
            g.fillRect(P_(dirv > 0.f ? -0.03f : 0.01f, -0.46f).x, P_(0.f, -0.46f).y, 0.07f * len, 0.11f * len, body);
            const float ww = std::max(1.2f, 0.022f * len), wh_ = std::max(1.f, 0.020f * len);
            for (int row = 0; row < 3; ++row) for (int w = 0; w < 14 - row * 2; ++w) {
                if (hashi(k * 7 + row * 31 + w, 88) < 0.22f) continue;
                const Pt q = P_(0.35f - row * 0.06f - w * 0.052f, -0.155f - row * 0.08f);
                g.fillRect(q.x, q.y, ww, wh_, hashi(k + w, 89) > 0.5f ? li : withA(Color{1.f, 0.85f, 0.55f, 1.f}, al));
            }
        } else {
            g.fillPolygon({P_(0.50f, -0.06f), P_(0.30f, 0.05f), P_(-0.50f, 0.05f), P_(-0.46f, -0.10f)}, body);
            const Pt h = P_(-0.05f, -0.30f); g.fillCircle(h.x, h.y, std::max(1.f, 0.14f * len), body);
            g.line(P_(-0.05f, -0.18f).x, P_(-0.05f, -0.18f).y, h.x, h.y, body, thin * 2.f, false);
        }
    }
}

void drawHelis(Frame& f, const Layer& L, float op) {
    Ctx& c = f.c; Gfx& g = c.g;
    const float W = c.W, H = c.H;
    const double t = f.t;
    const int n = (int)std::max(0.f, std::min(4.f, f.num(L.count, 1.f)));
    const int seed = (int)f.num(L.seed, 0.f) * 1013;
    const float y0 = f.num(L.y0, 0.3f) * H, y1 = f.num(L.y1, 0.45f) * H, sz = f.num(L.size, 0.07f) * H, ground = f.num(L.fly, 0.84f) * H;
    const double period = std::max(10.f, f.num(L.period, 70.f));
    const Color body = f.color(L.color, Color{0.07f, 0.02f, 0.14f, 1.f}), beam = f.color(L.color2, Color{1.f, 0.95f, 0.85f, 1.f}), nav = f.color(L.color3, Color{1.f, 0.25f, 0.3f, 1.f});
    for (int i = 0; i < n; ++i) {
        const double P = period * (0.9 + 0.2 * hashi(i + seed, 91));
        const double tt = t + hashi(i + seed, 92) * P * 5.0;
        const long long cyc = (long long)std::floor(tt / P);
        const double u = tt / P - (double)cyc;
        const int k = (int)(cyc % 100003) * 13 + i * 3 + seed;
        const float dirv = hashi(k, 93) > 0.5f ? 1.f : -1.f, ly = y0 + (y1 - y0) * hashi(k, 94);
        const float x = dirv > 0.f ? -0.08f * W + (float)u * 1.16f * W : 1.08f * W - (float)u * 1.16f * W;
        const float y = ly + std::sin(phase(t, 0.9, i)) * 0.006f * H;
        auto Q = [&](float uu, float vv) { return Pt{x + uu * sz * dirv, y + vv * sz}; };
        // the searchlight, swinging over the roofs and the water
        const Pt bel = Q(0.06f, 0.09f);
        const float tx = x + std::sin(phase(t, 0.42, i * 1.7)) * 0.17f * W + dirv * 0.04f * W, ty = ground + std::sin(phase(t, 0.31, i)) * 0.012f * H;
        const float hw = 0.034f * W * (0.85f + 0.2f * std::sin(phase(t, 1.9, i)));
        g.fillPolyVGradient({{bel.x - 2.f, bel.y}, {bel.x + 2.f, bel.y}, {tx + hw, ty}, {tx - hw, ty}}, bel.y, ty, withA(beam, 0.34f * op), withA(beam, 0.10f * op));
        g.glowEllipse(tx, ty, hw * 1.25f, 0.022f * H, withA(beam, 1.f), 0.55f * op);
        // the helicopter
        const Color b = withA(body, body.a * op);
        g.fillPolygon({Q(0.30f, 0.f), Q(0.24f, -0.09f), Q(0.05f, -0.13f), Q(-0.10f, -0.10f), Q(-0.16f, -0.04f), Q(-0.10f, 0.05f), Q(0.12f, 0.07f), Q(0.26f, 0.05f)}, b);
        g.fillPolygon({Q(-0.14f, -0.06f), Q(-0.62f, -0.045f), Q(-0.62f, -0.025f), Q(-0.14f, -0.01f)}, b);
        g.fillPolygon({Q(-0.58f, -0.045f), Q(-0.66f, -0.15f), Q(-0.70f, -0.15f), Q(-0.64f, -0.03f)}, b);
        const float th = std::max(1.f, 0.012f * sz);
        g.line(Q(0.18f, 0.12f).x, Q(0.18f, 0.12f).y, Q(-0.12f, 0.12f).x, Q(-0.12f, 0.12f).y, b, th, false);
        g.line(Q(0.10f, 0.06f).x, Q(0.10f, 0.06f).y, Q(0.10f, 0.12f).x, Q(0.10f, 0.12f).y, b, th, false);
        g.line(Q(-0.04f, 0.06f).x, Q(-0.04f, 0.06f).y, Q(-0.04f, 0.12f).x, Q(-0.04f, 0.12f).y, b, th, false);
        const Pt mast = Q(0.04f, -0.13f), top = Q(0.04f, -0.17f);
        g.line(mast.x, mast.y, top.x, top.y, b, th, false);
        const float rc = std::cos((float)std::fmod(t * 40.0 + i, 6.2831853));
        g.line(top.x - 0.56f * sz * rc, top.y, top.x + 0.56f * sz * rc, top.y, withA(body, body.a * op * 0.8f), th * 1.4f, false);
        g.strokeEllipse(top.x, top.y, 0.56f * sz, 0.02f * sz, withA(body, body.a * op * 0.25f), th);
        if (std::sin(phase(t, 5.0, i)) > 0.f) { const Pt tl = Q(-0.68f, -0.15f); g.fillCircle(tl.x, tl.y, std::max(1.2f, 0.022f * sz), withA(nav, nav.a * op)); }
        g.fillCircle(bel.x, bel.y, std::max(1.2f, 0.03f * sz), withA(beam, op));
    }
}

// ---------------------------------------------------------------- one layer
void drawLayer(Frame& f, const Layer& L) {
    Ctx& c = f.c; Gfx& g = c.g;
    const float W = c.W, H = c.H, s = c.s;
    const double t = f.t;
    if (!f.on(L)) return;
    const float op = clamp01(f.num(L.opacity, 1.f) * f.mul);
    if (op <= 0.003f) return;
    auto fade = [&](Color col) { col.a *= op; return col; };

    switch (L.kind) {
    // ---- gradient: bands of colour from y0 to y1 (fractions of the height), across the whole width
    case Kind::Gradient: {
        const float y0 = f.num(L.y0, 0.f) * H, y1 = f.num(L.y1, 1.f) * H, span = y1 - y0;
        const size_t n = L.colors.size();
        if (span <= 0.f || n < 2) return;
        std::vector<float> at(n);
        for (size_t i = 0; i < n; ++i) at[i] = i < L.at.size() ? (float)L.at[i] : (float)i / (float)(n - 1);
        const Color first = f.color(L.colors[0], Color{}), last = f.color(L.colors[n - 1], Color{});
        if (f.num(L.y0, 0.f) <= 0.0005f) g.fillRect(-16, y0 - 16, W + 32, 17.f, fade(first));        // a little past the edges: the burn-in shift moves the picture
        if (f.num(L.y1, 1.f) >= 0.9995f) g.fillRect(-16, y1 - 1.f, W + 32, 17.f, fade(last));
        for (size_t i = 0; i + 1 < n; ++i) {
            const float ya = y0 + span * at[i], yb = y0 + span * at[i + 1];
            if (yb <= ya) continue;
            g.vGradientRect(-16, ya, W + 32, yb - ya + 1.f, fade(f.color(L.colors[i], Color{})), fade(f.color(L.colors[i + 1], Color{})));
        }
    } break;

    // ---- rect: x y w h (+ radius for rounded corners)
    case Kind::Rect: {
        const float x = f.num(L.x) * W, y = f.num(L.y) * H, w = f.num(L.w, 1.f) * W, h = f.num(L.h, 1.f) * H, r = f.num(L.radius) * H;
        const Color col = fade(f.color(L.color, c.P.ink));
        if (L.thick.set && f.num(L.thick) > 0.f) g.strokeRoundRect(x, y, w, h, r, col, std::max(1.f, f.num(L.thick) * H));         // thick: an outline, not a fill
        else if (r > 0.5f) g.fillRoundRect(x, y, w, h, r, col); else g.fillRect(x, y, w, h, col);
    } break;

    // ---- circle: x y r; color2 makes it a top-to-bottom gradient; thick draws a ring instead of a disc
    case Kind::Circle: {
        const float x = f.num(L.x, 0.5f) * W, y = f.num(L.y, 0.5f) * H, r = f.num(L.r, 0.1f) * H;
        const Color c1 = fade(f.color(L.color, c.P.ink));
        if (L.thick.set && f.num(L.thick) > 0.f) g.strokeCircle(x, y, r, c1, std::max(1.f, f.num(L.thick) * H));
        else if (L.color2.set) g.fillCircleVGradient(x, y, r, c1, fade(f.color(L.color2, c.P.ink)));
        else g.fillCircle(x, y, r, c1);
    } break;

    // ---- image: a picture from the pack folder. Give w or h (the other follows the picture's shape); anchor decides which point of the picture sits at x,y
    case Kind::Image: {
        const bool had = themes::image(g, f.pk, L.src, false) != nullptr;
        if (!had && f.budget <= 0) return;
        const Bitmap* b = themes::image(g, f.pk, L.src, f.budget > 0);
        if (!b) return;
        if (!had) --f.budget;
        const float aspect = (float)b->w / (float)std::max(1, b->h);
        float w, h;
        if (L.w.set && L.h.set) { w = f.num(L.w) * W; h = f.num(L.h) * H; }
        else if (L.h.set) { h = f.num(L.h) * H; w = h * aspect; }
        else if (L.w.set) { w = f.num(L.w) * W; h = w / aspect; }
        else { w = (float)b->w * s; h = (float)b->h * s; }
        float ax, ay; anchorOf(L.anchor, ax, ay);
        const float px = f.num(L.x, 0.5f) * W, py = f.num(L.y, 0.5f) * H;
        const float x0 = px - ax * w, y0 = py - ay * h;
        const float rot = f.num(L.rotate);
        if (std::fabs(rot) < 0.001f && !L.flip) g.drawBitmapRect(*b, x0, y0, w, h, op);
        else {
            const bool center = L.pivot == "center";
            g.drawBitmapRot(*b, x0, y0, w, h, center ? x0 + w * 0.5f : px, center ? y0 + h * 0.5f : py, rot, op, L.flip);
        }
    } break;

    // ---- glow: a soft light. r for a round one, or rx and ry. The opacity is how strong it is
    case Kind::Glow: {
        const float x = f.num(L.x, 0.5f) * W, y = f.num(L.y, 0.5f) * H;
        const float r = f.num(L.r, 0.2f);
        const float rx = L.rx.set ? f.num(L.rx) * W : r * H, ry = L.ry.set ? f.num(L.ry) * H : r * H;
        const Color col = f.color(L.color, c.P.accent);
        g.glowEllipse(x, y, rx, ry, withA(col, 1.f), op * col.a);
    } break;

    // ---- stars: count small points that twinkle, between y0 and y1
    case Kind::Stars: {
        const int n = (int)std::max(0.f, std::min(500.f, f.num(L.count, 40.f)));
        const int seed = (int)f.num(L.seed, 0.f) * 1013;
        const float y0 = f.num(L.y0, 0.f) * H, y1 = std::max(y0 + 1.f, f.num(L.y1, 0.6f) * H);
        const float r0 = f.num(L.size, 0.0009f) * H, r1 = std::max(r0, f.num(L.size2, 0.0021f) * H);
        const float speed = f.num(L.speed, 0.5f) / 0.5f, fadeDown = f.num(L.fade, 0.6f);
        for (int i = 0; i < n; ++i) {
            const float x = hashi(i + seed, 31) * W, y = y0 + hashi(i + seed, 32) * (y1 - y0), u = (y - y0) / (y1 - y0);
            const float tw = 0.55f + 0.45f * std::sin(phase(t, (0.35 + 0.8 * hashi(i + seed, 33)) * speed, hashi(i + seed, 34) * 6.28));
            Color col = c.P.ink;
            if (!L.colors.empty()) {
                size_t k = 0; if (L.colors.size() > 1 && hashi(i + seed, 36) > 0.8f) k = 1 + (size_t)(i % (int)(L.colors.size() - 1));
                col = f.color(L.colors[k], c.P.ink);
            }
            g.fillCircle(x, y, r0 + (r1 - r0) * hashi(i + seed, 35), withA(col, col.a * 0.55f * tw * op * (1.f - fadeDown * u * 0.6f)));
        }
    } break;

    // ---- skyline: buildings along y (the ground line), up to h tall; colors are [far, near]; windows are [colour, colour]
    case Kind::Skyline: {
        const float hh = std::max(2.f, f.num(L.h, 0.13f) * H), base = f.num(L.y, 0.8f) * H;
        uint64_t key = 1469598103934665603ull;
        key = mix64(key, (uint64_t)W); key = mix64(key, (uint64_t)H); key = mix64(key, (uint64_t)hh);
        key = mix64(key, (uint64_t)(long long)(f.num(L.seed, 0.f) + 1.f));
        key = mix64(key, (uint64_t)(f.num(L.sides, 1.f) * 100.f));
        const Color kFar = L.colors.size() > 0 ? f.color(L.colors[0], Color{0.23f, 0.08f, 0.35f, 0.92f}) : Color{0.23f, 0.08f, 0.35f, 0.92f};
        const Color kNear = L.colors.size() > 1 ? f.color(L.colors[1], Color{0.10f, 0.03f, 0.20f, 1.f}) : Color{0.10f, 0.03f, 0.20f, 1.f};
        const Color kW1 = L.windows.size() > 0 ? f.color(L.windows[0], Color{1.f, 0.43f, 0.70f, 0.85f}) : Color{1.f, 0.43f, 0.70f, 0.85f};
        const Color kW2 = L.windows.size() > 1 ? f.color(L.windows[1], Color{0.2f, 0.9f, 0.83f, 0.8f}) : kW1;
        key = mix64(key, colKey(kFar)); key = mix64(key, colKey(kNear)); key = mix64(key, colKey(kW1)); key = mix64(key, colKey(kW2));
        if (!L.sprite.bmp || L.spriteKey != key) {
            if (f.budget <= 0) { if (!L.sprite.bmp) return; }
            else {
                --f.budget; g.freeBitmap(L.sprite);
                if (g.beginSprite((int)W, (int)hh)) { buildSkyline(g, f, L, W, hh); L.sprite = g.endSprite(); L.spriteKey = key; ++gSpriteBuilds; }
                if (!L.sprite.bmp) return;
            }
        }
        g.drawBitmapRect(L.sprite, -8.f, base - (float)L.sprite.h, W + 16.f, (float)L.sprite.h, op);
    } break;

    // ---- streaks: the glitter of a light on water: short bars that thin out and shimmer from y0 down to y1
    case Kind::Streaks: {
        const int n = (int)std::max(1.f, std::min(60.f, f.num(L.count, 14.f)));
        const float y0 = f.num(L.y0, 0.8f) * H, y1 = f.num(L.y1, 1.f) * H, cx = f.num(L.x, 0.5f) * W;
        const float w0 = f.num(L.w, 0.08f) * W, w1 = f.num(L.w2, 0.04f) * W, sp = f.num(L.speed, 1.f), sway = f.num(L.sway, 0.012f);
        const Color c0 = f.color(L.color, c.P.accent), c1 = L.color2.set ? f.color(L.color2, c.P.accent2) : c0;
        for (int k = 0; k < n; ++k) {
            const float u = n > 1 ? k / (float)(n - 1) : 0.f;
            const float y = y0 + (y1 - y0) * std::pow(u, 1.45f);
            const float hw = (w0 + (w1 - w0) * u) * (0.72f + 0.28f * std::sin(phase(t, (0.9 + 0.13 * k) * sp, k * 1.3)));
            const float x = cx + std::sin(phase(t, (0.35 + 0.05 * k) * sp, k * 0.9)) * sway * W * (0.4f + u);
            const float th = std::max(1.f, (f.num(L.thick, 0.0028f) + (f.num(L.thick2, 0.007f) - f.num(L.thick, 0.0028f)) * u) * H);
            Color col = lerp(c0, c1, u); col.a *= (0.70f - 0.40f * u) * op;
            g.line(x - hw, y, x + hw, y, col, th, false);
        }
    } break;

    // ---- text: {tokens} are replaced (see README). size is the font size, or cap is the height of capital letters; both as fractions of the height
    case Kind::Text: {
        std::string str = f.pick(L.text, L.period.set && f.num(L.period) > 0.f ? (long long)std::floor(t / f.num(L.period)) : 0);
        if (str.empty()) return;
        if (L.upper) str = util::upper(str);
        const FontId font = fontOf(L.font, F_MEDIUM);
        float em = L.cap.set ? f.num(L.cap) * H / std::max(0.01f, g.capHeight(font, 1.f)) : f.num(L.size, 0.03f) * H;
        const std::wstring ws = util::widen(str);
        const float track = f.num(L.tracking);
        // digits: 'gap' is the width of the colon relative to a digit (default 0.42), 'tracking' adds space after every character
        const float colon = f.num(L.gap, 0.42f);
        auto width = [&](float e) { return L.digits ? g.timeWidth(font, e, ws, colon, track) : g.textWidth(font, e, ws, track); };
        if (L.maxw.set) { const float tw = width(em), lim = f.num(L.maxw) * W; if (tw > lim && tw > 1.f) em *= lim / tw; }
        const float x = f.num(L.x, 0.5f) * W, y = f.num(L.y, 0.5f) * H;
        const Align al = L.align == "left" ? Align::Left : L.align == "right" ? Align::Right : Align::Center;
        const Color c1 = fade(f.color(L.color, c.P.ink));
        const bool neon = L.neon.set;
        if (neon) g.setNeon(withA(f.color(L.neon, f.pk.neonColor), 1.f), f.num(L.neonStrength, 1.f) * op);
        if (L.digits) g.timeText(font, em, x, y, ws, c1, L.color2.set ? fade(f.color(L.color2, c.P.ink2)) : c1, al, colon, track);
        else if (L.color2.set) { const float cap = g.capHeight(font, em); g.textGradient(font, em, x, y, ws, c1, fade(f.color(L.color2, c.P.ink2)), y - cap, y, al, track); }
        else g.text(font, em, x, y, ws, c1, al, track);
        if (neon) g.setNeon(f.pk.neonColor, c.P.neon);                                 // back to the theme's own bloom
    } break;

    // ---- shapes: a row of stars (or circles or diamonds); "lit" of them are filled. flash > 0 makes the lit ones alternate colour and color2 (police lights)
    case Kind::Shapes: {
        const int n = (int)std::max(1.f, std::min(12.f, f.num(L.count, 5.f)));
        const int lit = (int)std::floor(f.num(L.lit, (float)n) + 0.001f);
        const float r = f.num(L.size, 0.024f) * H, gap = r * f.num(L.gap, 2.7f), x0 = f.num(L.x, 0.5f) * W, y0 = f.num(L.y, 0.14f) * H;
        const float flash = f.num(L.flash), gl = f.num(L.glow, 0.6f), ow = std::max(1.f, f.num(L.outlineW, 0.0016f) * H);
        for (int i = 0; i < n; ++i) {
            const float x = x0 + (i - (n - 1) * 0.5f) * gap;
            const bool on = i < lit;
            Color col = f.color(L.color, c.P.accent);
            if (flash > 0.f && on) col = ((std::sin(phase(t, flash)) > 0) == (i % 2 == 0)) ? col : f.color(L.color2, c.P.accent2);
            const float pulse = on ? 0.78f + 0.22f * std::sin(phase(t, 2.2, i * 0.9)) : 0.f;
            if (on && gl > 0.f) g.glow(x, y0, r * 2.4f, withA(col, 1.f), gl * pulse * op * col.a);
            const Color edgeOn = fade(f.color(L.outline, withA(c.P.ink, 0.85f))), edgeOff = withA(edgeOn, edgeOn.a * 0.36f);
            const Color fillc = fade(col);
            if (L.shape == "circle") { if (on) g.fillCircle(x, y0, r, fillc); g.strokeCircle(x, y0, r, on ? edgeOn : edgeOff, ow); }
            else if (L.shape == "diamond") {
                std::vector<Pt> p = {{x, y0 - r}, {x + r * 0.7f, y0}, {x, y0 + r}, {x - r * 0.7f, y0}};
                if (on) g.fillPolygon(p, fillc);
                g.polyline(p, true, on ? edgeOn : edgeOff, ow, true);
            } else starShape(g, x, y0, r, fillc, on ? edgeOn : edgeOff, ow, on, 5);
        }
    } break;

    // ---- banner: a little plane drags a banner across, a new message each trip. period = seconds between trips, fly = seconds a trip takes
    case Kind::Banner: {
        const double period = std::max(5.f, f.num(L.period, 44.f)), fly = std::max(3.f, std::min((float)period, f.num(L.fly, 24.f)));
        const double cyc = std::floor(t / period), fr = t / period - cyc;
        if (fr * period > fly) return;
        const std::string msg = f.pick(L.text, (long long)cyc);
        if (msg.empty()) return;
        const FontId font = fontOf(L.font, F_MONO_M);
        const float fs = f.num(L.size, 0.024f) * H, bh = f.num(L.h, 0.046f) * H, py = f.num(L.y, 0.65f) * H;
        const std::wstring text = util::widen(msg);
        const float tw = g.textWidth(font, fs, text, 0.12f), Lb = tw + 0.07f * H, planeW = 0.11f * H, total = W + Lb + planeW + 0.05f * H;
        const float px = (float)((fr * period / fly) * total) - Lb - 0.03f * H;                  // the plane's tail
        const float cy = py + std::sin(phase(t, 1.1)) * 0.004f * H;
        const Color fill = f.color(L.color, Color{0.10f, 0.04f, 0.25f, 0.88f}), edge = f.color(L.color2, c.P.accent), ink = f.color(L.color3, c.P.ink);
        const float bx1 = px - 0.03f * H, bx0 = bx1 - Lb;
        std::vector<Pt> top, bot;
        for (int i = 0; i <= 14; ++i) {
            const float u = i / 14.f, x = bx0 + (bx1 - bx0) * u;
            const float w = std::sin(phase(t, 3.2) + u * 7.f) * 0.0045f * H * (1.f - u);       // the banner ripples
            top.push_back({x, cy - bh * 0.5f + w}); bot.push_back({x, cy + bh * 0.5f + w});
        }
        std::vector<Pt> poly = top; for (int i = (int)bot.size() - 1; i >= 0; --i) poly.push_back(bot[i]);
        g.fillPolygon(poly, fade(fill));
        g.polyline(top, false, fade(edge), std::max(1.f, 0.0022f * H), false);
        g.polyline(bot, false, fade(edge), std::max(1.f, 0.0022f * H), false);
        g.text(font, fs, bx0 + 0.035f * H, cy + g.capHeight(font, fs) * 0.5f, text, fade(ink), Align::Left, 0.12f);
        g.line(bx1, cy - bh * 0.3f, px + 0.012f * H, cy - 0.004f * H, fade(withA(ink, 0.7f)), std::max(1.f, 0.0016f * H), false);
        g.line(bx1, cy + bh * 0.3f, px + 0.012f * H, cy + 0.004f * H, fade(withA(ink, 0.7f)), std::max(1.f, 0.0016f * H), false);
        const Bitmap* pb = nullptr;
        if (!L.plane.empty()) {
            const bool had = themes::image(g, f.pk, L.plane, false) != nullptr;
            if (had || f.budget > 0) {
                pb = themes::image(g, f.pk, L.plane, f.budget > 0);
                if (pb && !had) --f.budget;
            }
        }
        if (pb) {                                                                            // your own plane picture, nose pointing right
            const float ph_ = f.num(L.planeH, 0.05f) * H, pw = ph_ * (float)pb->w / (float)std::max(1, pb->h);
            g.drawBitmapRect(*pb, px, cy - ph_ * 0.5f, pw, ph_, op);
        } else {                                                                             // the built-in little plane
            const float u = 0.012f * H;
            const Color body = fade(Color{1.f, 0.90f, 0.95f, 1.f}), fin = fade(edge), wing = fade(lerp(edge, ink, 0.35f));
            g.fillPolygon({{px, cy - u * 1.0f}, {px + u * 3.0f, cy - u * 0.9f}, {px + u * 9.5f, cy - u * 0.2f}, {px + u * 9.5f, cy + u * 0.6f}, {px + u * 2.0f, cy + u * 0.9f}, {px, cy + u * 0.4f}}, body);
            g.fillPolygon({{px, cy - u * 0.9f}, {px - u * 0.9f, cy - u * 2.6f}, {px + u * 1.6f, cy - u * 0.9f}}, fin);
            g.fillPolygon({{px + u * 4.2f, cy}, {px + u * 3.2f, cy + u * 2.4f}, {px + u * 5.6f, cy + u * 0.2f}}, wing);
            g.fillRect(px + u * 9.5f, cy - u * 0.5f, u * 0.35f, u * 1.0f, fade(withA(ink, 0.8f)));
            g.strokeEllipse(px + u * 9.9f, cy + u * 0.05f, u * 0.2f, u * (0.9f + 0.4f * std::sin(phase(t, 60.0))), fade(withA(ink, 0.5f)), std::max(1.f, 0.002f * H));
        }
    } break;

    // ---- confetti: little bars falling and turning. Switch it on only when wanted with "when"
    case Kind::Confetti: {
        const int n = (int)std::max(1.f, std::min(300.f, f.num(L.count, 48.f)));
        const int seed = (int)f.num(L.seed, 0.f) * 1013;
        const float sp = f.num(L.speed, 1.f), sz = f.num(L.size, 0.008f) * H, sway = f.num(L.sway, 0.03f);
        for (int i = 0; i < n; ++i) {
            const float x = hashi(i + seed, 41) * W + std::sin(phase(t, 0.6 + hashi(i + seed, 42), i)) * sway * W;
            double yy = std::fmod((0.10 + 0.16 * hashi(i + seed, 43)) * H * sp * t + hashi(i + seed, 44) * H * 1.2, H * 1.2); if (yy < 0) yy += H * 1.2;
            const float y = (float)yy - 0.1f * H;
            Color col = L.colors.empty() ? c.P.accent : f.color(L.colors[(size_t)i % L.colors.size()], c.P.accent);
            const float rot = phase(t, 2.0 + 3.0 * hashi(i + seed, 45), i), w = sz * (1.f + hashi(i + seed, 46));
            g.line(x - std::cos(rot) * w * 0.5f, y - std::sin(rot) * w * 0.25f, x + std::cos(rot) * w * 0.5f, y + std::sin(rot) * w * 0.25f, withA(col, col.a * 0.85f * op), std::max(2.f, 3.f * s), false);
        }
    } break;

    // ---- shade: weather darkening on top of everything so far: color at alpha, and a grey mist (color2) at alpha2. Alphas are usually formulas of gloom and haze
    case Kind::Shade: {
        const float a1 = clamp01(f.num(L.alpha) * op), a2 = clamp01(f.num(L.alpha2) * op);
        if (a1 > 0.002f) { Color d = f.color(L.color, Color{0.03f, 0.02f, 0.08f, 1.f}); d.a *= a1; g.fillRect(-16, -16, W + 32, H + 32, d); }
        if (a2 > 0.002f) { Color m = f.color(L.color2, Color{0.56f, 0.55f, 0.64f, 1.f}); m.a *= a2; g.fillRect(-16, -16, W + 32, H + 32, m); }
    } break;

    // ---- line: from (x,y) to (x2,y2)
    case Kind::Line: {
        g.line(f.num(L.x) * W, f.num(L.y) * H, f.num(L.x2) * W, f.num(L.y2) * H, fade(f.color(L.color, c.P.line)), std::max(1.f, f.num(L.thick, 0.002f) * H), true);
    } break;

    // ---- button: a clickable box x y w h. A click (or its "key") sets the state variable 'set' to 'to'. Draws nothing, except a hover tint if it has colour / color2
    case Kind::Button: {
        App& a = c.a;
        const float x = f.num(L.x) * W, y = f.num(L.y) * H, w = f.num(L.w) * W, h = f.num(L.h) * H, r = f.num(L.radius) * H;
        const float mx = a.mouseX - a.shiftX, my = a.mouseY - a.shiftY;
        const bool hov = !a.showSettings && !a.showHelp && !a.isSaver && a.mouseX >= 0 && mx >= x && mx < x + w && my >= y && my < y + h;
        if (hov && L.color.set) { const Color fc = fade(f.color(L.color, c.P.accent)); if (r > 0.5f) g.fillRoundRect(x, y, w, h, r, fc); else g.fillRect(x, y, w, h, fc); }
        if (hov && L.color2.set) g.strokeRoundRect(x, y, w, h, r, fade(f.color(L.color2, c.P.accent2)), std::max(1.f, 1.5f * s));
        if (L.slotSet >= 0) {
            const double val = f.num(L.to, 1.f);
            const std::string key = f.pk.id + "/" + L.set;
            App* ap = &a;
            auto press = [ap, key, val] { ap->packState[key] = {val, ap->now}; };
            a.addAction(x + a.shiftX, y + a.shiftY, w, h, press);
            if (!L.key.empty()) {
                int vk = L.key == "tab" ? 0x09 : L.key == "space" ? 0x20 : L.key == "enter" ? 0x0D : L.key == "up" ? 0x26 : L.key == "down" ? 0x28 : 0;
                if (vk) a.packKeys.push_back({vk, press});
            }
        }
    } break;

    // ---- map: countries from a map file, drawn in a box (x, y, w; the height follows the map's shape). countries: ["554", ...] or ["*"]
    case Kind::Map: drawMapLayer(f, L, op); break;

    // ---- traffic: jets with trails, boats with wakes, a police helicopter with a searchlight
    case Kind::Traffic:
        if (L.vehicle == "jet") drawJets(f, L, op); else if (L.vehicle == "boat") drawBoats(f, L, op); else drawHelis(f, L, op);
        break;
    }
}

void drawScene(Ctx& c, Pack& pk, const themes::Scene& sc, float mul) {
    if (sc.layers.empty() || mul <= 0.002f) return;
    Frame f(c, pk, mul);
    f.hoverPass(sc);
    for (const Layer& L : sc.layers) drawLayer(f, L);
    c.g.setNeon(pk.neonColor, c.P.neon);
}

} // namespace

// ---------------------------------------------------------------- called from App::render
void drawThemeBackdrop(Ctx& c, themes::Pack& p, bool full, float mul) { drawScene(c, p, full ? p.backdrop : p.backdropLite, mul); }
void drawThemeFace(Ctx& c, themes::Pack& p, themes::Face& face) { drawScene(c, p, face.scene, 1.f); }

} // namespace app
