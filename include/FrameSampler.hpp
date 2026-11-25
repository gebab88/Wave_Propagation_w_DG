#ifndef FRAMESAMPLER_HPP
#define FRAMESAMPLER_HPP

#include <armadillo>

// Samples a time-dependent field at fixed times t_k = t0 + k * interval by
// linear interpolation between the solver steps around each t_k.
class FrameSampler {
public:
    static constexpr double kMaxFrames = 1000000;

    // K + 1 with K = floor((t1 - t0) / interval + 1e-9). Returned as double so
    // that a tiny interval cannot overflow; callers check it against kMaxFrames.
    static double frameCount(double t0, double t1, double interval);

    // Requires t1 > t0, interval > 0, frameCount(t0, t1, interval) <= kMaxFrames.
    FrameSampler(double t0, double t1, double interval, arma::uword valuesPerFrame);

    void start(const arma::vec &values);   // frame 0 = values (state at t0)

    // Fills every frame k with tPrev < t_k <= t by linear interpolation
    // between prev (at tPrev) and cur (at t); frames already filled stay.
    void advance(double tPrev, const arma::vec &prev, double t, const arma::vec &cur);

    arma::uword filled() const { return filled_; }       // frames filled so far
    const arma::mat &frames() const { return frames_; }  // row = frame, column = value
    const arma::vec &times() const { return times_; }    // min(t0 + k * interval, t1)

private:
    arma::mat frames_;
    arma::vec times_;
    arma::uword filled_;
};

#endif
