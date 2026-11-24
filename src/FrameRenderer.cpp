#include "FrameRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

constexpr int FrameRenderer::kWidth;
constexpr int FrameRenderer::kHeight;

namespace {

const int kLeft = 60, kRight = 772, kTop = 15, kBottom = 568;
const int kTickLength = 6;
const int kMarkerArm = 3;
const int kFontScale = 2;
const int kGlyphW = 5, kGlyphH = 7;

struct Color {
    std::uint8_t r, g, b;
};
const Color kBlack = {0, 0, 0};
const Color kGrey = {160, 160, 160};
const Color kViolet = {148, 0, 211};

// 5x7 glyphs, one byte per row, bit 4 = leftmost column.
struct Glyph {
    char ch;
    std::uint8_t rows[kGlyphH];
};
const Glyph kGlyphs[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
};

const int kCharAdvance = (kGlyphW + 1) * kFontScale;
const int kTextHeight = kGlyphH * kFontScale;

bool hasGlyph(char ch) {
    for (const Glyph &g : kGlyphs)
        if (g.ch == ch) return true;
    return false;
}

// Width of the drawable glyphs only; drawText skips characters without a glyph
// and does not advance for them.
int textWidth(const std::string &s) {
    int n = 0;
    for (char ch : s) n += hasGlyph(ch) ? 1 : 0;
    return n == 0 ? 0 : n * kCharAdvance - kFontScale;
}

std::string formatTick(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Interior of the plot box: where curve pixels may be drawn.
bool inInterior(int x, int y) {
    return x > kLeft && x < kRight && y > kTop && y < kBottom;
}

void setPixel(std::uint8_t *img, int x, int y, Color c) {
    if (x < 0 || x >= FrameRenderer::kWidth || y < 0 || y >= FrameRenderer::kHeight) return;
    std::uint8_t *p = img + (static_cast<std::size_t>(y) * FrameRenderer::kWidth + x) * 3;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
}

// Blend colour c over the existing pixel with coverage a in [0,1]; interior only.
void blendInterior(std::uint8_t *img, int x, int y, Color c, double a) {
    if (!inInterior(x, y) || a <= 0.0) return;
    a = std::min(a, 1.0);
    std::uint8_t *p = img + (static_cast<std::size_t>(y) * FrameRenderer::kWidth + x) * 3;
    p[0] = static_cast<std::uint8_t>(std::lround(p[0] * (1.0 - a) + c.r * a));
    p[1] = static_cast<std::uint8_t>(std::lround(p[1] * (1.0 - a) + c.g * a));
    p[2] = static_cast<std::uint8_t>(std::lround(p[2] * (1.0 - a) + c.b * a));
}

void drawText(std::uint8_t *img, const std::string &s, int left, int top) {
    int pos = 0;  // index among drawable glyphs
    for (std::size_t i = 0; i < s.size(); ++i) {
        for (const Glyph &g : kGlyphs) {
            if (g.ch != s[i]) continue;
            for (int row = 0; row < kGlyphH; ++row)
                for (int col = 0; col < kGlyphW; ++col) {
                    if (!((g.rows[row] >> (kGlyphW - 1 - col)) & 1)) continue;
                    for (int dy = 0; dy < kFontScale; ++dy)
                        for (int dx = 0; dx < kFontScale; ++dx)
                            setPixel(img,
                                     left + pos * kCharAdvance + col * kFontScale + dx,
                                     top + row * kFontScale + dy, kBlack);
                }
            ++pos;
            break;
        }
    }
}

// Liang-Barsky: clip the segment to the rectangle; false if nothing remains.
bool clipSegment(double &x0, double &y0, double &x1, double &y1,
                 double xlo, double ylo, double xhi, double yhi) {
    const double dx = x1 - x0, dy = y1 - y0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0 - xlo, xhi - x0, y0 - ylo, yhi - y0};
    double t0 = 0.0, t1 = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
        } else {
            const double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > t1) return false;
                t0 = std::max(t0, t);
            } else {
                if (t < t0) return false;
                t1 = std::min(t1, t);
            }
        }
    }
    const double nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy;
    const double nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
}

// Pixel-space coordinates are clamped to a safe range so deltas and Liang-Barsky
// parameters stay finite; the plot box is < 1000 px, so in-range data is unchanged.
double clampPx(double v) { return std::max(-1e6, std::min(1e6, v)); }

double fpart(double v) { return v - std::floor(v); }

// Xiaolin Wu antialiased line. Endpoints must already be clipped to the box.
void wuLine(std::uint8_t *img, double x0, double y0, double x1, double y1, Color c) {
    const bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const double dx = x1 - x0, dy = y1 - y0;
    const double gradient = dx == 0.0 ? 1.0 : dy / dx;

    auto plot = [&](int a, int b, double cov) {
        if (steep) blendInterior(img, b, a, c, cov);
        else blendInterior(img, a, b, c, cov);
    };

    const int xs = static_cast<int>(std::floor(x0));
    const int xe = static_cast<int>(std::floor(x1));
    for (int x = xs; x <= xe; ++x) {
        // Portion of the line's x-extent that falls into pixel column x.
        const double cx0 = std::max(x0, static_cast<double>(x));
        const double cx1 = std::min(x1, static_cast<double>(x + 1));
        if (cx1 < cx0 || (cx1 == cx0 && dx != 0.0 && x != xe)) continue;
        const double w = dx == 0.0 ? 1.0 : (cx1 - cx0);
        const double yc = dx == 0.0 ? y0 : y0 + gradient * ((cx0 + cx1) * 0.5 - x0);
        const int yi = static_cast<int>(std::floor(yc));
        const double f = fpart(yc);
        plot(x, yi, (1.0 - f) * w);
        plot(x, yi + 1, f * w);
    }
}

}  // namespace

std::vector<double> niceTicks(double lo, double hi, int maxTicks) {
    if (!(hi > lo)) return std::vector<double>(1, lo);
    const double range = hi - lo;
    const double tol = 1e-9 * range;
    const double mults[] = {1.0, 2.0, 2.5, 5.0};
    double decade = std::pow(10.0, std::floor(std::log10(range / maxTicks)) - 1.0);
    for (int guard = 0; guard < 40; ++guard, decade *= 10.0) {
        for (double m : mults) {
            const double step = m * decade;
            const double kLo = std::ceil(lo / step - tol / step);
            const double kHi = std::floor(hi / step + tol / step);
            const double count = kHi - kLo + 1.0;
            if (count < 1.0 || count >= maxTicks) continue;
            std::vector<double> ticks;
            for (double k = kLo; k <= kHi; k += 1.0) ticks.push_back(k == 0.0 ? 0.0 : k * step);  // avoid a "-0" label
            return ticks;
        }
    }
    return std::vector<double>(1, lo);
}

FrameRenderer::FrameRenderer(double xmin, double xmax, double ymin, double ymax)
    : xmin_(xmin), xmax_(xmax), ymin_(ymin), ymax_(ymax),
      background_(frameBytes(), 255) {
    std::uint8_t *img = background_.data();

    const std::vector<double> xt = niceTicks(xmin_, xmax_, 11);
    const std::vector<double> yt = niceTicks(ymin_, ymax_, 11);

    // Dashed grid (4 px on, 4 px off) at the ticks, inside the box.
    for (double v : xt) {
        const int px = static_cast<int>(std::lround(toPixelX(v)));
        if (px <= kLeft || px >= kRight) continue;
        for (int y = kTop + 1; y < kBottom; ++y)
            if (((y - kTop - 1) / 4) % 2 == 0) setPixel(img, px, y, kGrey);
    }
    for (double v : yt) {
        const int py = static_cast<int>(std::lround(toPixelY(v)));
        if (py <= kTop || py >= kBottom) continue;
        for (int x = kLeft + 1; x < kRight; ++x)
            if (((x - kLeft - 1) / 4) % 2 == 0) setPixel(img, x, py, kGrey);
    }

    // Box.
    for (int x = kLeft; x <= kRight; ++x) {
        setPixel(img, x, kTop, kBlack);
        setPixel(img, x, kBottom, kBlack);
    }
    for (int y = kTop; y <= kBottom; ++y) {
        setPixel(img, kLeft, y, kBlack);
        setPixel(img, kRight, y, kBlack);
    }

    // Ticks, drawn inward on all four sides, plus labels left and below.
    for (double v : xt) {
        const int px = static_cast<int>(std::lround(toPixelX(v)));
        if (px < kLeft || px > kRight) continue;
        for (int d = 1; d <= kTickLength; ++d) {
            setPixel(img, px, kBottom - d, kBlack);
            setPixel(img, px, kTop + d, kBlack);
        }
        const std::string s = formatTick(v);
        drawText(img, s, px - textWidth(s) / 2, kBottom + 8);
    }
    for (double v : yt) {
        const int py = static_cast<int>(std::lround(toPixelY(v)));
        if (py < kTop || py > kBottom) continue;
        for (int d = 1; d <= kTickLength; ++d) {
            setPixel(img, kLeft + d, py, kBlack);
            setPixel(img, kRight - d, py, kBlack);
        }
        const std::string s = formatTick(v);
        drawText(img, s, kLeft - 10 - textWidth(s), py - kTextHeight / 2);
    }
}

std::size_t FrameRenderer::frameBytes() const {
    return static_cast<std::size_t>(kWidth) * kHeight * 3;
}

const std::vector<std::uint8_t> &FrameRenderer::background() const { return background_; }

int FrameRenderer::plotLeft() const { return kLeft; }
int FrameRenderer::plotRight() const { return kRight; }
int FrameRenderer::plotTop() const { return kTop; }
int FrameRenderer::plotBottom() const { return kBottom; }

double FrameRenderer::toPixelX(double x) const {
    const double range = xmax_ - xmin_;
    if (!(range > 0.0)) return kLeft;
    return kLeft + (x - xmin_) / range * (kRight - kLeft);
}

double FrameRenderer::toPixelY(double y) const {
    const double range = ymax_ - ymin_;
    if (!(range > 0.0)) return kBottom;
    return kBottom - (y - ymin_) / range * (kBottom - kTop);
}

void FrameRenderer::render(const double *x, const double *y, std::size_t n,
                           std::uint8_t *out) const {
    std::memcpy(out, background_.data(), background_.size());

    const double xlo = kLeft + 1, xhi = kRight - 1, ylo = kTop + 1, yhi = kBottom - 1;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) ||
            !std::isfinite(x[i + 1]) || !std::isfinite(y[i + 1]))
            continue;
        double ax = toPixelX(x[i]), ay = toPixelY(y[i]);
        double bx = toPixelX(x[i + 1]), by = toPixelY(y[i + 1]);
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by))
            continue;
        ax = clampPx(ax); ay = clampPx(ay); bx = clampPx(bx); by = clampPx(by);
        if (clipSegment(ax, ay, bx, by, xlo, ylo, xhi, yhi) &&
            std::isfinite(ax) && std::isfinite(ay) && std::isfinite(bx) && std::isfinite(by))
            wuLine(out, ax, ay, bx, by, kViolet);
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) continue;
        const double fx = toPixelX(x[i]), fy = toPixelY(y[i]);
        if (!(fx >= xlo && fx <= xhi && fy >= ylo && fy <= yhi)) continue;
        const int cx = static_cast<int>(std::lround(fx)), cy = static_cast<int>(std::lround(fy));
        for (int d = -kMarkerArm; d <= kMarkerArm; ++d) {
            blendInterior(out, cx + d, cy, kViolet, 1.0);
            blendInterior(out, cx, cy + d, kViolet, 1.0);
        }
    }
}
