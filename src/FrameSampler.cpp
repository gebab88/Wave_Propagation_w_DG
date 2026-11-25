#include "FrameSampler.hpp"

#include <cmath>

// Out-of-class definition: required in C++14 when the member is odr-used.
constexpr double FrameSampler::kMaxFrames;

double FrameSampler::frameCount(double t0, double t1, double interval) {
    return std::floor((t1 - t0) / interval + 1e-9) + 1.0;
}

FrameSampler::FrameSampler(double t0, double t1, double interval, arma::uword valuesPerFrame)
    : filled_(0) {
    const arma::uword n = static_cast<arma::uword>(frameCount(t0, t1, interval));
    frames_.zeros(n, valuesPerFrame);
    times_.set_size(n);
    for (arma::uword k = 0; k < n; ++k)
        times_(k) = std::fmin(t0 + static_cast<double>(k) * interval, t1);
}

void FrameSampler::start(const arma::vec &values) {
    frames_.row(0) = values.t();
    filled_ = 1;
}

void FrameSampler::advance(double tPrev, const arma::vec &prev, double t, const arma::vec &cur) {
    while (filled_ < frames_.n_rows && times_(filled_) <= t) {
        const double alpha = (times_(filled_) - tPrev) / (t - tPrev);
        frames_.row(filled_) = ((1.0 - alpha) * prev + alpha * cur).t();
        ++filled_;
    }
}
