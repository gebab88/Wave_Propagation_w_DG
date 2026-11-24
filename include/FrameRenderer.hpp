#ifndef FRAMERENDERER_HPP
#define FRAMERENDERER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

// "Nice" tick positions in [lo, hi]: multiples of a step from {1, 2, 2.5, 5}*10^k,
// using the smallest step that yields fewer than maxTicks ticks.
// lo == hi returns {lo}.
std::vector<double> niceTicks(double lo, double hi, int maxTicks);

// Draws one pressure-vs-x frame into an rgb24 buffer (800x600, top row first).
// Pure computation: no I/O, no Armadillo. The static part (box, dashed grid,
// ticks, tick labels) is rendered once in the constructor; render() copies it
// and draws the curve on top.
class FrameRenderer {
public:
    static constexpr int kWidth = 800, kHeight = 600;

    FrameRenderer(double xmin, double xmax, double ymin, double ymax);

    std::size_t frameBytes() const;                       // kWidth*kHeight*3
    const std::vector<std::uint8_t> &background() const;  // rgb24, row-major, top row first

    // Writes frameBytes() bytes to out. Non-finite points break the curve;
    // everything is clipped to the plot box interior.
    void render(const double *x, const double *y, std::size_t n, std::uint8_t *out) const;

    int plotLeft() const;    // box edges in px
    int plotRight() const;
    int plotTop() const;
    int plotBottom() const;

    double toPixelX(double x) const;
    double toPixelY(double y) const;

private:
    double xmin_, xmax_, ymin_, ymax_;
    std::vector<std::uint8_t> background_;
};

#endif
