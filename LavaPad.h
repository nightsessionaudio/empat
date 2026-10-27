#pragma once
// Лава-лампа для пэда: метаболлы в низком разрешении, цвет капель привязан к настроению угла.
// Чистый C++ без зависимостей от JUCE (рендерит в буфер RGB).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lava {
constexpr int W = 176, H = 80;
constexpr float AS = 2.2f; // W / H

// Палитра Smoke LCD (как в HTML-референсе).
// Ethereal (верх-лево), Toxic (верх-право), Candlelight (низ-лево), 4 AM (низ-право), центр
static const unsigned PAL[5][3] = {
  { 0x8fa3dc, 0xb6c0ee, 0xb6c0ee }, { 0x9cc84a, 0xc4df7c, 0xc4df7c },
  { 0xe48f3c, 0xf0b868, 0xf0b868 }, { 0x3b50bd, 0x5a4cb4, 0x5a4cb4 },
  { 0xe6d2c2, 0xf2e8de, 0xe6d2c2 } };

struct Blob { int m; float c[3]; float ax, ay, r, s, ph, a0, a1; };

struct Field {
  Field() { init(); }
  void kick() { surge = 1.0f; }
  void pulse() { flash = 1.0f; }

  // tu, tv: позиция точки (0..1, v вниз); human, taste: 0..1; dt в секундах
  void render(uint8_t* out, float dt, float tu, float tv, float human, float taste) {
    ex += (tu - ex) * (1.0f - std::exp(-dt / 0.24f));
    ey += (tv - ey) * (1.0f - std::exp(-dt / 0.24f));
    surge *= std::exp(-dt / 1.1f); flash *= std::exp(-dt / 0.32f);
    const float hum = 0.6f + human * 0.8f, tz = 0.85f + taste * 0.3f, sp = 1.0f + surge * 2.2f, sat = 0.55f + taste * 0.5f;
    const float wt[4] = { (1 - ex) * (1 - ey), ex * (1 - ey), (1 - ex) * ey, ex * ey };
    const float wc = std::max(0.0f, 1.0f - std::hypot(ex - 0.5f, ey - 0.5f) * 2.2f);
    const int n = (int) B.size();
    float px[32], py[32], rq[32], cr[32], cg[32], cb[32];
    for (int i = 0; i < n; ++i) {
      Blob& b = B[(size_t) i];
      b.a0 += dt * b.s * sp; b.a1 += dt * b.s * 0.8f * sp;
      const float w = b.m < 0 ? wc : wt[b.m];
      float x = b.ax + 0.07f * hum * std::sin(b.a0 + b.ph);
      float y = b.ay + 0.13f * hum * std::sin(b.a1 + b.ph * 1.3f + surge * 2.0f);
      const float pull = b.m < 0 ? 0.35f * wc : 0.2f * w;
      x += (ex - x) * pull; y += (ey - y) * pull;
      const float rr = b.r * (b.m < 0 ? 0.6f + 0.6f * wc : 0.55f + 0.8f * w) * tz * (1.0f + surge * 0.12f);
      const float br = (b.m < 0 ? 0.62f : 0.72f) + 0.4f * w;
      px[i] = x * AS; py[i] = y; rq[i] = rr * rr;
      cr[i] = b.c[0] * br; cg[i] = b.c[1] * br; cb[i] = b.c[2] * br;
    }
    for (int y = 0; y < H; ++y) {
      const float Y = ((float) y + 0.5f) / (float) H;
      for (int x = 0; x < W; ++x) {
        const float X = ((float) x + 0.5f) / (float) W * AS;
        float f = 0, R = 0, G = 0, Bc = 0;
        for (int i = 0; i < n; ++i) {
          const float a = X - px[i], c = Y - py[i], k = rq[i] / (a * a + c * c + 0.0015f);
          f += k; R += k * cr[i]; G += k * cg[i]; Bc += k * cb[i];
        }
        const int q = (y * W + x) * 3;
        float r = bg[(size_t) q], g = bg[(size_t) q + 1], b = bg[(size_t) q + 2];
        if (f > 0.3f) {
          const float inv = 1.0f / f;
          const float sh = 0.62f + 0.38f * std::min(1.0f, std::max(0.0f, (f - 1.0f) / 1.6f)) + flash * 0.3f;
          float wx = std::min(1.0f, std::max(0.0f, (f - 0.85f) / 0.3f)); wx = wx * wx * (3.0f - 2.0f * wx);
          const float d = (f - 1.0f) / 0.17f, rim = std::exp(-d * d) * 0.22f;
          const float gl = std::min(1.0f, std::max(0.0f, (f - 0.3f) / 0.55f)) * 0.16f * (1.0f - wx);
          const float wr = R * inv * sh, wg = G * inv * sh, wb = Bc * inv * sh;
          r += (wr - r) * wx + wr * (rim + gl); g += (wg - g) * wx + wg * (rim + gl); b += (wb - b) * wx + wb * (rim + gl);
        }
        const float gray = 0.213f * r + 0.715f * g + 0.072f * b;
        r = gray + (r - gray) * sat; g = gray + (g - gray) * sat; b = gray + (b - gray) * sat;
        const int o = (y * W + x) * 3;
        out[o] = (uint8_t) std::min(255.0f, std::max(0.0f, r));
        out[o + 1] = (uint8_t) std::min(255.0f, std::max(0.0f, g));
        out[o + 2] = (uint8_t) std::min(255.0f, std::max(0.0f, b));
      }
    }
  }

private:
  void add(int m, unsigned c, float ax, float ay, float r, float s, float ph) {
    Blob b; b.m = m; b.c[0] = (float) ((c >> 16) & 255); b.c[1] = (float) ((c >> 8) & 255); b.c[2] = (float) (c & 255);
    b.ax = ax; b.ay = ay; b.r = r; b.s = s; b.ph = ph; b.a0 = 0; b.a1 = 0; B.push_back(b);
  }
  void init() {
    static const int cn[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    for (int i = 0; i < 4; ++i) {
      const bool a = cn[i][0] != 0, b = cn[i][1] != 0; const float fi = (float) i;
      add(i, PAL[i][0], a ? .86f : .14f, b ? .8f : .2f, .27f, .35f, fi * 1.7f);
      add(i, PAL[i][1], a ? .68f : .32f, b ? .6f : .4f, .17f, .5f, fi * 2.3f + 1.0f);
      add(i, PAL[i][2], a ? .95f : .05f, b ? .42f : .58f, .1f, .7f, fi * .9f + 2.0f);
    }
    add(-1, PAL[4][0], .5f, .5f, .16f, .45f, 0.0f); add(-1, PAL[4][1], .42f, .6f, .12f, .55f, 2.1f); add(-1, PAL[4][2], .58f, .42f, .1f, .65f, 4.2f);
    static const float BG[4][3] = { { 22, 28, 58 }, { 20, 34, 16 }, { 44, 22, 10 }, { 10, 14, 44 } };
    static const float LIFT[3] = { 30, 20, 22 };
    bg.resize((size_t) (W * H * 3));
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float u = (float) x / (float) W, v = (float) y / (float) H;
        const float l = std::max(0.0f, 1.0f - std::hypot(u - 0.5f, (v - 0.5f) * 0.9f) * 2.1f);
        const float hv = std::hypot(u - 0.5f, v - 0.5f) * 1.4f, vg = 1.0f - 0.35f * hv * hv;
        for (int c = 0; c < 3; ++c) {
          const float t = BG[0][c] * (1 - u) + BG[1][c] * u, b = BG[2][c] * (1 - u) + BG[3][c] * u;
          bg[(size_t) ((y * W + x) * 3 + c)] = ((t * (1 - v) + b * v) + LIFT[c] * l * 0.7f) * vg;
        }
      }
  }
  std::vector<Blob> B; std::vector<float> bg;
  float ex = 0.62f, ey = 0.38f, surge = 0.0f, flash = 0.0f;
};
}
