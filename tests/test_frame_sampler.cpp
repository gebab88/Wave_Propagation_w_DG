// Plain C++ checks for FrameSampler (no test framework in this project).
#include "FrameSampler.hpp"
#include "SystemMemory.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) <= 1e-12; }

static void report(const char *name, int before) {
    if (failures == before) std::printf("PASS %s\n", name);
}

static void test_frame_count() {
    const int before = failures;
    CHECK(FrameSampler::frameCount(0, 0.02, 1e-3 / 60) == 1201);
    CHECK(FrameSampler::frameCount(0, 8e-5, 1e-3 / 60) == 5);
    CHECK(FrameSampler::frameCount(0, 8e-5, 1.0 / 60) == 1);
    CHECK(FrameSampler::frameCount(0, 1.0, 0.25) == 5);
    const double huge = FrameSampler::frameCount(0, 1.0, 1e-12);
    CHECK(huge > FrameSampler::kMaxFrames);
    CHECK(huge < std::numeric_limits<double>::infinity());
    report("test_frame_count", before);
}

static void test_times_start_at_t0() {
    const int before = failures;
    FrameSampler s(1.0, 2.0, 0.25, 1);
    const double expected[] = {1.0, 1.25, 1.5, 1.75, 2.0};
    CHECK(s.times().n_elem == 5);
    for (arma::uword k = 0; k < s.times().n_elem && k < 5; ++k)
        CHECK(near(s.times()(k), expected[k]));
    report("test_times_start_at_t0", before);
}

static void test_several_frames_per_step() {
    const int before = failures;
    FrameSampler s(0, 1.0, 0.25, 2);
    arma::vec v0 = {0, 10}, v1 = {4, 6};
    s.start(v0);
    s.advance(0, v0, 1.0, v1);
    CHECK(s.filled() == 5);
    CHECK(s.frames().n_rows == 5 && s.frames().n_cols == 2);
    for (arma::uword k = 0; k < 5 && k < s.frames().n_rows; ++k) {
        CHECK(near(s.frames()(k, 0), double(k)));
        CHECK(near(s.frames()(k, 1), 10.0 - double(k)));
    }
    report("test_several_frames_per_step", before);
}

static void test_solver_like_loop_fills_all_frames() {
    const int before = failures;
    FrameSampler s(0, 1.0, 0.25, 1);
    double t = 0.0, tPrev = 0.0;
    s.start(arma::vec{t * t});
    do {
        tPrev = t;
        t += 0.1;
        s.advance(tPrev, arma::vec{tPrev * tPrev}, t, arma::vec{t * t});
        // A frame is filled exactly when its step reaches it.
        arma::uword expected = 0;
        for (arma::uword k = 0; k < s.times().n_elem; ++k)
            if (s.times()(k) <= t) ++expected;
        CHECK(s.filled() == expected);
        // The value is the linear interpolation between the bracketing steps
        // (t*t is not linear, so a wrong pair of steps gives another value).
        for (arma::uword k = 0; k < s.filled(); ++k) {
            const double tk = s.times()(k);
            if (tk > tPrev && tk <= t) {
                const double want = tPrev * tPrev + (tk - tPrev) / (t - tPrev) * (t * t - tPrev * tPrev);
                CHECK(near(s.frames()(k, 0), want));
            }
        }
    } while (t < 1.0);
    CHECK(s.filled() == 5);
    report("test_solver_like_loop_fills_all_frames", before);
}

static void test_last_time_not_after_t1() {
    const int before = failures;
    const double cases[3][2] = {{1.0, 1.0 / 3}, {1.0, 0.1}, {0.02, 1e-3 / 60}};
    for (const auto &c : cases) {
        FrameSampler s(0, c[0], c[1], 1);
        CHECK(s.times()(s.times().n_elem - 1) <= c[0]);
    }
    report("test_last_time_not_after_t1", before);
}

static void test_advance_after_last_frame_is_noop() {
    const int before = failures;
    FrameSampler s(0, 1.0, 0.25, 1);
    s.start(arma::vec{0.0});
    s.advance(0, arma::vec{0.0}, 1.0, arma::vec{1.0});
    CHECK(s.filled() == 5);
    const arma::mat copy = s.frames();
    s.advance(1.0, arma::vec{1.0}, 1.1, arma::vec{99.0});
    CHECK(s.filled() == 5);
    CHECK(arma::approx_equal(copy, s.frames(), "absdiff", 0.0));
    report("test_advance_after_last_frame_is_noop", before);
}

static bool rel_near(double a, double b) { return std::fabs(a - b) <= 1e-12 * std::fabs(b); }

static void test_buffer_bytes() {
    const int before = failures;
    CHECK(FrameSampler::bufferBytes(1201, 250) == 2411608);
    CHECK(rel_near(FrameSampler::bufferBytes(480001, 800000), 480001.0 * 800001.0 * 8.0));
    const double huge = FrameSampler::bufferBytes(1e6, 1e9);
    CHECK(huge < std::numeric_limits<double>::infinity());
    CHECK(huge > 8e15);
    report("test_buffer_bytes", before);
}

static void test_max_buffer_bytes() {
    const int before = failures;
    CHECK(rel_near(FrameSampler::maxBufferBytes(8e9), 4.8e9));
    CHECK(FrameSampler::maxBufferBytes(0) == 4.0e9);
    CHECK(FrameSampler::maxBufferBytes(-1) == 4.0e9);
    report("test_max_buffer_bytes", before);
}

static void test_installed_ram_is_plausible() {
    const int before = failures;
    const double ram = installedRamBytes();
    CHECK(ram > 1e9);
    CHECK(ram < 1e15);
    report("test_installed_ram_is_plausible", before);
}

int main() {
    test_frame_count();
    test_times_start_at_t0();
    test_several_frames_per_step();
    test_solver_like_loop_fills_all_frames();
    test_last_time_not_after_t1();
    test_advance_after_last_frame_is_noop();
    test_buffer_bytes();
    test_max_buffer_bytes();
    test_installed_ram_is_plausible();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
