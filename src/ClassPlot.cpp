#include "ClassPlot.hpp"
#include "FrameRenderer.hpp"
#include "VideoPipe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

constexpr int ClassPlot::kFps;

ClassPlot::ClassPlot(const mat &pressure, const vec &TimeSteps, const vec &xnodes,
                     double x0, double x1)
    : pressure(pressure), TimeSteps(TimeSteps), xnodes(xnodes), x0(x0), x1(x1)
{
}

ClassPlot::~ClassPlot()
{
    //dtor
}

bool ClassPlot::RenderVideo(const std::string &filename)
{
    const std::size_t nframes = TimeSteps.size();
    if (nframes == 0) return false;

    if (pressure.n_rows != nframes || pressure.n_cols != xnodes.size()) {
        std::cout << "Plotting failed: pressure data has shape "
                  << pressure.n_rows << "x" << pressure.n_cols
                  << ", expected " << nframes << "x" << xnodes.size()
                  << std::endl;
        return false;
    }

    std::cout << "Rendering " << nframes << " frames at " << kFps << " fps" << std::endl;

    const std::size_t npts = xnodes.size();
    std::vector<double> xs(xnodes.memptr(), xnodes.memptr() + npts);
    const FrameRenderer renderer(x0, x1, -2.0, 2.0);
    const std::size_t fbytes = renderer.frameBytes();

    unsigned nthreads = std::thread::hardware_concurrency();
    nthreads = (nthreads == 0) ? 4u : std::min(nthreads, 8u);  // cap batch memory
    const std::size_t batch = 4 * static_cast<std::size_t>(nthreads);
    std::vector<std::uint8_t> buffer(batch * fbytes);

    const std::string cmd =
        "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s "
        + std::to_string(FrameRenderer::kWidth) + "x"
        + std::to_string(FrameRenderer::kHeight) + " "
        "-framerate " + std::to_string(kFps) + " -i - -c:v libx264 -preset veryfast -pix_fmt yuv420p "
        + shellQuote(filename);
    std::fflush(stdout);
    FILE *pipe = openWritePipe(cmd);
    if (!pipe) {
        std::cout << "Video encoding failed (could not start ffmpeg). "
                     "Is ffmpeg on the PATH?" << std::endl;
        return false;
    }

    // A dead ffmpeg must surface as a short write, not kill us with SIGPIPE.
    SigpipeIgnored sigpipeGuard;
    bool writeOk = true;

    for (std::size_t first = 0; first < nframes && writeOk; first += batch) {
        const std::size_t count = std::min(batch, nframes - first);

        // Rows of the column-major matrix are strided: each worker copies its
        // row into a contiguous buffer before rendering into its buffer slot.
        auto worker = [&](unsigned tid) {
            std::vector<double> row(npts);
            for (std::size_t j = tid; j < count; j += nthreads) {
                for (std::size_t k = 0; k < npts; ++k)
                    row[k] = pressure(first + j, k);
                renderer.render(xs.data(), row.data(), npts,
                                buffer.data() + j * fbytes);
            }
        };
        {
            std::vector<std::thread> pool;
            pool.reserve(nthreads);
            unsigned started = 0;
            try {
                for (unsigned t = 0; t < nthreads; ++t) {
                    pool.emplace_back(worker, t);
                    ++started;
                }
            } catch (const std::system_error &) {
                // Could not start every thread: the missing slices are
                // rendered below on the calling thread.
            }
            for (auto &th : pool)
                th.join();
            for (unsigned t = started; t < nthreads; ++t)
                worker(t);
        }

        if (std::fwrite(buffer.data(), fbytes, count, pipe) != count)
            writeOk = false;
    }

    const PipeStatus status = closeWritePipe(pipe);

    if (status.kind != PipeStatus::Exited || status.value != 0) {
        std::cout << "Video encoding failed (";
        if (status.kind == PipeStatus::Exited)
            std::cout << "ffmpeg returned " << status.value
                      << "). Is ffmpeg on the PATH?";
        else if (status.kind == PipeStatus::Signaled)
            std::cout << "ffmpeg killed by signal " << status.value << ").";
        else
            std::cout << "ffmpeg failed, status " << status.value << ").";
        std::cout << std::endl;
        return false;
    }
    if (!writeOk) {
        std::cout << "Video encoding failed (could not write all frames to ffmpeg)."
                  << std::endl;
        return false;
    }
    std::cout << "Video written to " << filename << std::endl;
    return true;
}
