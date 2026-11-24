#include "ClassPlot.hpp"
#include "FrameRenderer.hpp"
#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

    std::cout << "Rendering " << nframes << " frames at 60 fps" << std::endl;

    const std::size_t npts = xnodes.size();
    std::vector<double> xs(xnodes.memptr(), xnodes.memptr() + npts);
    const FrameRenderer renderer(x0, x1, -2.0, 2.0);
    const std::size_t fbytes = renderer.frameBytes();

    unsigned nthreads = std::thread::hardware_concurrency();
    if (nthreads == 0) nthreads = 4;
    const std::size_t batch = 4 * static_cast<std::size_t>(nthreads);
    std::vector<std::uint8_t> buffer(batch * fbytes);

    const std::string cmd =
        "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s 800x600 "
        "-framerate 60 -i - -c:v libx264 -preset veryfast -pix_fmt yuv420p '"
        + filename + "'";
    std::fflush(stdout);
    FILE *pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        std::cout << "Video encoding failed (could not start ffmpeg). "
                     "Is ffmpeg on the PATH?" << std::endl;
        return false;
    }

    // A dead ffmpeg must surface as a short write, not kill us with SIGPIPE.
    void (*oldHandler)(int) = std::signal(SIGPIPE, SIG_IGN);
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
            for (unsigned t = 0; t < nthreads; ++t)
                pool.emplace_back(worker, t);
            for (auto &th : pool)
                th.join();
        }

        if (std::fwrite(buffer.data(), fbytes, count, pipe) != count)
            writeOk = false;
    }

    std::signal(SIGPIPE, oldHandler);
    int ret = pclose(pipe);
    if (ret == 0 && !writeOk) ret = -1;

    if (ret != 0) {
        std::cout << "Video encoding failed (ffmpeg returned " << ret
                  << "). Is ffmpeg on the PATH?" << std::endl;
        return false;
    }
    std::cout << "Video written to " << filename << std::endl;
    return true;
}
