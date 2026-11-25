#ifndef CLASSPLOT_H
#define CLASSPLOT_H


#include <armadillo>
#include <string>
using namespace arma;

class ClassPlot
{
    public:
        static constexpr int kFps = 60;   // frame rate of the video

        // Keeps references to the arguments: they must outlive this object.
        ClassPlot(const mat &pressure, const vec &TimeSteps, const vec &xnodes,
                  double x0, double x1);
        ~ClassPlot();
        // Renders one frame per row of the pressure matrix and streams them as raw rgb24
        // into ffmpeg. Returns true on success.
        bool RenderVideo(const std::string &filename = "output.mp4");

    protected:

    private:
        const mat &pressure;
        const vec &TimeSteps;
        const vec &xnodes;
        double x0;
        double x1;
};

#endif // CLASSPLOT_H
