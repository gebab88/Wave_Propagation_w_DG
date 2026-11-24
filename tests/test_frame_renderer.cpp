// Plain C++ checks for FrameRenderer (no test framework in this project).
#include "FrameRenderer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

struct Rgb {
    int r, g, b;
    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

static Rgb pixel(const std::vector<std::uint8_t> &img, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * FrameRenderer::kWidth + x) * 3;
    return {img[i], img[i + 1], img[i + 2]};
}

static bool approxEqual(const std::vector<double> &a, const std::vector<double> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::fabs(a[i] - b[i]) > 1e-12) return false;
    return true;
}

static void report(const char *name, int before) {
    if (failures == before) std::printf("PASS %s\n", name);
}

static void test_nice_ticks() {
    const int before = failures;
    CHECK(approxEqual(niceTicks(0.0, 0.5, 11), {0, 0.1, 0.2, 0.3, 0.4, 0.5}));
    CHECK(approxEqual(niceTicks(-2.0, 2.0, 11),
                      {-2, -1.5, -1, -0.5, 0, 0.5, 1, 1.5, 2}));
    auto t2 = niceTicks(0.0, 2.0, 11);
    CHECK(t2.size() >= 4 && t2.size() <= 11);
    auto t3 = niceTicks(0.0, 0.37, 11);
    CHECK(t3.size() >= 4 && t3.size() <= 11);
    CHECK(niceTicks(1.0, 1.0, 11).size() == 1);
    report("test_nice_ticks", before);
}

static void test_background_layout() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    const auto &bg = r.background();
    CHECK(bg.size() == r.frameBytes());
    const Rgb black{0, 0, 0}, white{255, 255, 255}, grey{160, 160, 160};
    CHECK(pixel(bg, r.plotLeft(), 300) == black);
    CHECK(pixel(bg, 100, 100) == white);

    const int gx = static_cast<int>(std::lround(r.toPixelX(0.1)));
    bool foundGrey = false;
    for (int y = r.plotTop() + 1; y < r.plotBottom(); ++y)
        if (pixel(bg, gx, y) == grey) foundGrey = true;
    CHECK(foundGrey);

    const int lx = static_cast<int>(std::lround(r.toPixelX(0.3)));
    bool foundLabel = false;
    for (int y = r.plotBottom() + 1; y < FrameRenderer::kHeight; ++y)
        for (int x = lx - 15; x <= lx + 15; ++x)
            if (!(pixel(bg, x, y) == white)) foundLabel = true;
    CHECK(foundLabel);
    report("test_background_layout", before);
}

static void test_render_curve() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    std::vector<std::uint8_t> out(r.frameBytes());
    const double x[] = {0, 0.25, 0.5}, y[] = {0, 0, 0};
    r.render(x, y, 3, out.data());
    const Rgb p = pixel(out, static_cast<int>(std::lround(r.toPixelX(0.125))),
                        static_cast<int>(std::lround(r.toPixelY(0))));
    CHECK(p.r > p.g && p.b > p.g);

    r.render(x, y, 0, out.data());
    CHECK(out == r.background());
    report("test_render_curve", before);
}

static void test_nonfinite_and_huge_values() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    std::vector<std::uint8_t> out(r.frameBytes());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double x[] = {0, 0.1, 0.2, 0.3, 0.4, 0.5};
    const double y[] = {nan, inf, -inf, 1e300, -1e300, 0};
    r.render(x, y, 6, out.data());
    const auto &bg = r.background();
    bool same = true;
    for (int py = 0; py < FrameRenderer::kHeight; ++py)
        for (int px = 0; px < FrameRenderer::kWidth; ++px) {
            const bool outside = px <= r.plotLeft() || px >= r.plotRight() ||
                                 py <= r.plotTop() || py >= r.plotBottom();
            if (outside && !(pixel(out, px, py) == pixel(bg, px, py))) same = false;
        }
    CHECK(same);
    report("test_nonfinite_and_huge_values", before);
}

static bool outsideUnchanged(const FrameRenderer &r, const std::vector<std::uint8_t> &out) {
    const auto &bg = r.background();
    for (int py = 0; py < FrameRenderer::kHeight; ++py)
        for (int px = 0; px < FrameRenderer::kWidth; ++px) {
            const bool outside = px <= r.plotLeft() || px >= r.plotRight() ||
                                 py <= r.plotTop() || py >= r.plotBottom();
            if (outside && !(pixel(out, px, py) == pixel(bg, px, py))) return false;
        }
    return true;
}

static void test_extreme_finite_values() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    std::vector<std::uint8_t> out(r.frameBytes());
    const double x[] = {0, 0.1, 0.2, 0.3, 0.4, 0.5};
    const double y[] = {1e306, -1e306, 1e306, -1e306, 1e306, -1e306};
    r.render(x, y, 6, out.data());
    CHECK(outsideUnchanged(r, out));
    const double xe[] = {-1e306, 1e306, 0.25};
    const double ye[] = {1e306, -1e306, 0.0};
    r.render(xe, ye, 3, out.data());
    CHECK(outsideUnchanged(r, out));
    report("test_extreme_finite_values", before);
}

static bool violetish(const std::vector<std::uint8_t> &out, const std::vector<std::uint8_t> &bg,
                      int x, int y) {
    const Rgb p = pixel(out, x, y);
    return p.r > p.g && p.b > p.g && !(p == pixel(bg, x, y));
}

static void test_marker_drawn() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    std::vector<std::uint8_t> out(r.frameBytes());
    const double x[] = {0.25}, y[] = {1.0};
    r.render(x, y, 1, out.data());
    const int cx = static_cast<int>(std::lround(r.toPixelX(0.25)));
    const int cy = static_cast<int>(std::lround(r.toPixelY(1.0)));
    const auto &bg = r.background();
    CHECK(violetish(out, bg, cx - 3, cy));
    CHECK(violetish(out, bg, cx + 3, cy));
    CHECK(violetish(out, bg, cx, cy - 3));
    CHECK(violetish(out, bg, cx, cy + 3));
    report("test_marker_drawn", before);
}

static void test_segment_crossing_edge() {
    const int before = failures;
    FrameRenderer r(0, 0.5, -2, 2);
    std::vector<std::uint8_t> out(r.frameBytes());
    const double x[] = {0.1, 0.4}, y[] = {0.0, 5.0};
    r.render(x, y, 2, out.data());
    const int row = r.plotTop() + 2;
    bool found = false;
    const int x0 = static_cast<int>(r.toPixelX(0.1)), x1 = static_cast<int>(r.toPixelX(0.4));
    for (int px = x0; px <= x1; ++px)
        if (violetish(out, r.background(), px, row)) found = true;
    CHECK(found);
    CHECK(outsideUnchanged(r, out));
    report("test_segment_crossing_edge", before);
}

int main() {
    test_nice_ticks();
    test_background_layout();
    test_render_curve();
    test_nonfinite_and_huge_values();
    test_extreme_finite_values();
    test_marker_drawn();
    test_segment_crossing_edge();
    return failures == 0 ? 0 : 1;
}
