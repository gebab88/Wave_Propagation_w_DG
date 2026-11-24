#include <iostream>
#include <string>
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <armadillo>
#include <stdlib.h>
#include <stdio.h>
#include <chrono>
#include <stdexcept>
#include <climits>

#include <csignal>
#include <cerrno>
#include <termios.h>
#include <unistd.h>

#include <omp.h>
#include <yaml-cpp/yaml.h>

#include "InitialAndBoundary.hpp"
#include "ClassdXdt.hpp"
#include "vandermonde.hpp"
#include "ClassPlot.hpp"

using namespace arma;
using namespace std;

// Read a required scalar from the YAML config. yaml-cpp's exceptions for a
// missing key or a wrong-typed value are anonymous and, if uncaught, abort the
// process via std::terminate. This wrapper names the offending key (and shows
// the bad value) and exits cleanly with status 1 instead.
template <typename T>
static T cfgGet(const YAML::Node &cfg, const char *key) {
    if (!cfg[key]) {
        cerr << "Config error: missing key '" << key << "'" << endl;
        exit(EXIT_FAILURE);
    }
    try {
        return cfg[key].as<T>();
    } catch (const YAML::Exception &) {
        cerr << "Config error: key '" << key << "' has an invalid value";
        if (cfg[key].IsScalar())
            cerr << " ('" << cfg[key].Scalar() << "')";
        cerr << endl;
        exit(EXIT_FAILURE);
    }
}

template <typename T>
static T cfgGetOr(const YAML::Node &cfg, const char *key, const T &fallback) {
    if (!cfg[key])
        return fallback;
    try {
        return cfg[key].as<T>();
    } catch (const YAML::Exception &) {
        cerr << "Config error: key '" << key << "' has an invalid value";
        if (cfg[key].IsScalar())
            cerr << " ('" << cfg[key].Scalar() << "')";
        cerr << endl;
        exit(EXIT_FAILURE);
    }
}

// Time integrators selectable at startup, in menu order, with the one-line
// description shown next to each entry.
struct TimeIntegratorInfo {
    const char *name;
    const char *description;
};
static const std::array<TimeIntegratorInfo, 5> kTimeIntegrators = {{
    {"EulerExplicit",          "Euler, 1st order, needs small CFL"},
    {"RungeKuttaClassic",      "classic RK4, 4th order"},
    {"RungeKuttaSecond",       "Kutta 3/8 rule, 4th order"},
    {"RungeKuttaSecondOrder",  "Heun, 2nd order, cheapest at default CFL"},
    {"PredictorCorrectorHeun", "RK4 + iterated trapezoid, slowest"},
}};

static bool isKnownTimeIntegrator(const std::string &name) {
    for (const TimeIntegratorInfo &known : kTimeIntegrators)
        if (name == known.name)
            return true;
    return false;
}

// Terminal state to restore if SIGINT arrives while the terminal is in raw mode.
static struct termios g_savedTermios;

// Only async-signal-safe calls: restore the terminal, re-arm the default
// action and re-raise, so the process still dies from SIGINT (status 130).
static void restoreTerminalOnSigint(int) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTermios);
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}

// Read a single byte from stdin without waiting for Enter or echoing it.
// Returns 0-255, or -1 on EOF/error. The terminal state and the previous
// SIGINT disposition are always restored.
static int readKeyRaw() {
    struct termios saved;
    if (tcgetattr(STDIN_FILENO, &saved) != 0)
        return -1;
    g_savedTermios = saved;

    // Install the handler only if SIGINT is not ignored (nohup, trap "" INT).
    struct sigaction oldAct;
    bool handlerInstalled = false;
    if (sigaction(SIGINT, nullptr, &oldAct) == 0 && oldAct.sa_handler != SIG_IGN) {
        struct sigaction act{};
        act.sa_handler = restoreTerminalOnSigint;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        handlerInstalled = (sigaction(SIGINT, &act, nullptr) == 0);
    }

    struct termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    cerr.flush();
    unsigned char c = 0;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &c, 1);
    } while (n < 0 && errno == EINTR);

    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    if (handlerInstalled)
        sigaction(SIGINT, &oldAct, nullptr);
    return n == 1 ? static_cast<int>(c) : -1;
}

// Ask for the time integrator with one keypress. Without a terminal on stdin
// (</dev/null, pipes) or when running as a background job (reading the
// terminal would stop us with SIGTTIN/SIGTTOU) the config value is used silently.
static std::string selectTimeIntegration(const std::string &configDefault) {
    if (!isatty(STDIN_FILENO) || tcgetpgrp(STDIN_FILENO) != getpgrp()) {
        cout << "Using: " << configDefault << endl;
        return configDefault;
    }

    // Menu goes to stderr so it stays visible when stdout is redirected to a file.
    const std::string keyRange = "1-" + std::to_string(kTimeIntegrators.size());
    // Descriptions start in one column: longest name plus two spaces.
    size_t nameWidth = 0;
    for (const TimeIntegratorInfo &info : kTimeIntegrators)
        nameWidth = std::max(nameWidth, std::strlen(info.name));
    cerr << "Select time integration method:" << endl;
    for (size_t i = 0; i < kTimeIntegrators.size(); ++i) {
        const TimeIntegratorInfo &info = kTimeIntegrators[i];
        cerr << "  " << (i + 1) << ") " << info.name
             << std::string(nameWidth - std::strlen(info.name) + 2, ' ')
             << info.description;
        if (configDefault == info.name)
            cerr << "   (config.yaml)";
        cerr << endl;
    }

    std::string chosen;
    while (true) {
        cerr << "Press " << keyRange << ", Enter = config.yaml value: " << flush;
        const int key = readKeyRaw();
        if (key >= '1' && key < '1' + static_cast<int>(kTimeIntegrators.size())) {
            chosen = kTimeIntegrators[key - '1'].name;
            break;
        }
        if (key == '\n' || key == '\r' || key == 0x04 || key == -1) {
            chosen = configDefault;
            break;
        }
        tcflush(STDIN_FILENO, TCIFLUSH);  // drop the rest of an escape sequence
        cerr << endl << "Invalid key, press " << keyRange << " or Enter." << endl;
    }
    cerr << endl;
    cout << "Using: " << chosen << endl;
    return chosen;
}


int main( int argc, char *argv[] ){
    chrono::time_point<chrono::system_clock> StartTime, EndTimeCalc,EndTimePlot;

    // ---- Runtime configuration (replaces the former compile-time include/config.hpp) ----
    // Independent parameters are read from a YAML file (argv[1], else "config.yaml"
    // in the CWD). Derived quantities (Npoints, Z_0, omega, x, dx, Jac) are computed
    // here. Variable names/types match the old config.hpp, so the rest of main() is
    // unchanged.
    // Resolve the config path: an explicit argv[1] wins; otherwise look for
    // config.yaml in the CWD, then next to the binary (repo root, two levels above
    // bin/Release/) so `cd run && ../bin/Release/...` works without an argument.
    std::string configPath = (argc > 1) ? std::string(argv[1]) : std::string("config.yaml");
    YAML::Node cfg;
    try {
        cfg = YAML::LoadFile(configPath);
    } catch (const std::exception &e) {
        bool recovered = false;
        if (argc <= 1) {   // fall back to the config shipped next to the executable
            std::string exe = argv[0];
            auto slash = exe.find_last_of('/');
            std::string exeDir = (slash == std::string::npos) ? std::string(".") : exe.substr(0, slash);
            std::string alt = exeDir + "/../../config.yaml";
            try { cfg = YAML::LoadFile(alt); configPath = alt; recovered = true; }
            catch (const std::exception &) {}
        }
        if (!recovered) {
            cerr << "Error: could not read config '" << configPath << "': " << e.what() << "\n"
                 << "Pass the path as the first argument, or place config.yaml in the CWD "
                    "(or next to the binary), e.g. `cp config_example.yaml config.yaml`." << endl;
            return 1;
        }
    }

    const std::string configTimeIntegration = cfgGet<std::string>(cfg, "TimeIntegration");
    if (!isKnownTimeIntegrator(configTimeIntegration)) {
        cerr << "Config error: unknown TimeIntegration '" << configTimeIntegration << "'" << endl;
        return 1;
    }
    const std::string TimeIntegration = selectTimeIntegration(configTimeIntegration);
    StartTime = chrono::system_clock::now();   // exclude the time spent in the menu

    const float  CFLReserve = cfgGet<float>(cfg, "CFLReserve");

    // Read N and order as signed so a negative value is rejected here instead of
    // wrapping to a huge unsigned (-> bad_alloc / out-of-bounds). N < 2 underflows
    // the inner-cell loop (k = 1 .. N-2); order < 1 yields an empty node set.
    const long long N_in     = cfgGet<long long>(cfg, "N");
    const long long order_in = cfgGet<long long>(cfg, "order");
    if (N_in < 2) {
        cerr << "Config error: N must be >= 2 (got " << N_in << ")" << endl;
        return 1;
    }
    if (order_in < 1) {
        cerr << "Config error: order must be >= 1 (got " << order_in << ")" << endl;
        return 1;
    }
    const uword  N       = static_cast<uword>(N_in);
    const uword  Npoints = N + 1;
    const uword  order   = static_cast<uword>(order_in);

    const double t0 = cfgGet<double>(cfg, "t0");
    const double t1 = cfgGet<double>(cfg, "t1");
    const float  x0 = cfgGet<float>(cfg, "x0");
    const float  x1 = cfgGet<float>(cfg, "x1");

    vec x = linspace<vec>(x0, x1, Npoints);
    const double dx  = x[1] - x[0];
    const double Jac = dx / 2.0;

    const float  c_0   = cfgGet<float>(cfg, "c_0");
    const float  rho_0 = cfgGet<float>(cfg, "rho_0");
    const float  Z_0   = c_0 * rho_0;

    const double freq_i = cfgGet<double>(cfg, "freq_i");
    const double omega  = 2 * M_PI * freq_i;

    const double mu     = cfgGet<double>(cfg, "mu");
    const double sigma  = cfgGet<double>(cfg, "sigma");
    const double height = cfgGet<double>(cfg, "height");

    const bool EnablePlotting = cfgGetOr<bool>(cfg, "EnablePlotting", true);
    const long long OpenMPMinCells_in = cfgGetOr<long long>(cfg, "OpenMPMinCells", 50);
    if (OpenMPMinCells_in < 0) {
        cerr << "Config error: OpenMPMinCells must be >= 0 (got "
             << OpenMPMinCells_in << ")" << endl;
        return 1;
    }
    long long OpenMPThreads_in = cfgGetOr<long long>(cfg, "OpenMPThreads", 2);
    const long long OpenMPLargeMinCells_in = cfgGetOr<long long>(cfg, "OpenMPLargeMinCells", 512);
    const long long OpenMPLargeThreads_in = cfgGetOr<long long>(cfg, "OpenMPLargeThreads", 4);
    if (OpenMPThreads_in <= 0) {
        cerr << "Config error: OpenMPThreads must be >= 1 (got "
             << OpenMPThreads_in << ")" << endl;
        return 1;
    }
    if (OpenMPLargeMinCells_in < 0) {
        cerr << "Config error: OpenMPLargeMinCells must be >= 0 (got "
             << OpenMPLargeMinCells_in << ")" << endl;
        return 1;
    }
    if (OpenMPLargeThreads_in <= 0) {
        cerr << "Config error: OpenMPLargeThreads must be >= 1 (got "
             << OpenMPLargeThreads_in << ")" << endl;
        return 1;
    }

    uword OpenMPMinCells = static_cast<uword>(OpenMPMinCells_in);
    if (OpenMPLargeMinCells_in > 0 && N_in >= OpenMPLargeMinCells_in) {
        OpenMPMinCells = static_cast<uword>(OpenMPLargeMinCells_in);
        OpenMPThreads_in = OpenMPLargeThreads_in;
    }
    const uword OpenMPThreads = static_cast<uword>(OpenMPThreads_in);

    if (OpenMPThreads > static_cast<uword>(INT_MAX)) {
        cerr << "Config error: OpenMPThreads is too large (got "
             << OpenMPThreads_in << ")" << endl;
        return 1;
    }
    omp_set_num_threads(static_cast<int>(OpenMPThreads));
    // ------------------------------------------------------------------------------------

    float lambda_1 = c_0;
    float lambda_2 = -c_0;

    mat R = {{Z_0 , -Z_0}, {1.0 ,1.0}};
    mat R_inv= { {pow(2 * Z_0, -1) ,  0.5}, { (-1) * pow(2 * Z_0, -1) ,  0.5}};
    mat R_inv_calc=inv(R);

    mat lambda_plus = {{lambda_1, 0.0},{0.0 ,0.0}};
    mat lambda_minus ={{0.0 ,  0.0},{0.0 ,  lambda_2}};

    // Jacobi Matrices
    mat Aplus = R * lambda_plus * R_inv;
    mat Aminus = R * lambda_minus * R_inv;

    mat Q;
    vec invMdiag;
    mat U0;
    mat A=eye<mat>(2,2);

    vec xnodes={ };

    {
    InitialAndBoundary InitialBoundaryObj(height,Z_0,mu,sigma);
    U0 = zeros<mat>(2,order*N);
    switch (order)
    {
    case 1:
        {
            vec ui(2,fill::zeros);
            vec xi(order,fill::zeros);
            for (uword i=0; i<N; i++) {
                vec xi={0.5*(x[i]+x[i+1])};
                xnodes=join_cols(xnodes,xi);
                InitialBoundaryObj.U_init(ui,order); //Initialisierung mit 0
                //InitialBoundaryObj.U_init(xi,ui); //Initialisierung mit GaussGlocke
                //ui.print("ui =");
                U0.col(i)=ui;
            }
            Q=zeros<mat>(2,2);
            invMdiag=ones<vec>(2*order*N);
        //U0.print("U0= ");
        }break;

    case 2:
        {
        mat ui(2,2,fill::zeros);
        vec xi(order,fill::zeros);
        vec Stuetz={-sqrt(1.0/3.0) ,  sqrt(1.0/3.0)};
        for (uword i=0; i<N; i++) {
            //Stuetz.print("Stuetz=");
            vec xi=0.5*(x[i]*ones(order)+x[i+1]*ones(order) + dx * Stuetz);
            //xi.print("xi=");
            xnodes=join_cols(xnodes,xi);
            InitialBoundaryObj.U_init(ui,order); //Initialisierung mit 0
            //InitialBoundaryObj.U_init(xi,ui); //Initialisierung mit GaussGlocke
            //ui.print("ui=");
            U0.cols(i*order,i*order+1)=ui;

            //ui.print("ui =");
            //ui.save("ui.dat",raw_ascii);
            //a.print("a=");
            //b.print("b=");
            //Stuetz.print("Stütz=");
            //xi.print("xi: ");
        }
            // Inverse mass matrix is the identity for two support points.
            invMdiag=ones<vec>(2*order*N);
//
//          Berechnung des Flussintegrals, Steifigkeitsmatrix
            Q = kron(mat({{-1, -1},{ 1, 1}}), eye<mat>(2,2));
            Q=sqrt(3.0)/2.0 * Q;

            mat Funktionalmat={{0 ,  Z_0*c_0},{pow(rho_0,-1), 0}};
            Funktionalmat=kron(eye<mat>(2,2),Funktionalmat);

            Q=Funktionalmat*Q;
            Q.print("Q=");
            //Q.save("Q.dat",raw_ascii);
            } break;
    case 5:
        {
        mat ui(2,order,fill::zeros);
        vec xi(order,fill::zeros);

        double z1=-1.0/3.0*sqrt(5+2*sqrt(10.0/7.0));
        double z2=-1.0/3.0*sqrt(5-2*sqrt(10.0/7.0));
        double z3=0.0;
        double z4=1.0/3.0*sqrt(5-2*sqrt(10.0/7.0));
        double z5=1.0/3.0*sqrt(5+2*sqrt(10.0/7.0));

        vec Stuetz={z1,z2,z3,z4,z5};
        //Stuetz.print("z= ");

        for (uword i=0; i<N; i++) {
            //Stuetz.print("Stuetz=");
            vec xi=0.5*(x[i]*ones(order)+x[i+1]*ones(order) + dx * Stuetz);
            //xi.print("xi=");
            xnodes=join_cols(xnodes,xi);
            InitialBoundaryObj.U_init(ui,order); //Initialisierung mit 0
            //InitialBoundaryObj.U_init(xi,ui); //Initialisierung mit GaussGlocke
            //ui.print("ui=");
            U0.cols(i*order,(i+1)*order-1)=ui;

            //ui.print("ui =");
            //ui.save("ui.dat",raw_ascii);
            //a.print("a=");
            //b.print("b=");
            //Stuetz.print("Stütz=");
            //xi.print("xi: ");
        }

//          // Aufbau der Massenmatrix
            vec alpha(5);
            alpha[0]= (322-13*sqrt(70.0))/900.0;
            alpha[1]= (322+13*sqrt(70))/900.0;
            alpha[2]=128.0/225.0;
            alpha[3]=alpha[1];
            alpha[4]=alpha[0];
            //alpha.print("alpha = " );

            vec Mdiag=kron(eye<mat>(order,order),ones<vec>(2))*alpha;
            mat M=diagmat(Mdiag);
            //M.print("M= ");
            invMdiag=repmat(1.0/Mdiag, N, 1); // diagonal of kron(I_N, inv(M))
//
//          Berechnung des Flussintegrals, Steifigkeitsmatrix
            // Berechnung der transpornierten Differentationsmatrix
            mat DT=TransposeDiffMatrix(order,Stuetz);
            DT=kron(DT,eye<mat>(2,2));
            // Berechnung der Steifigkeitsmatrix
            Q=DT*M;

            mat Funktionalmat={{0 ,  Z_0*c_0},{pow(rho_0,-1), 0}};
            Funktionalmat=kron(eye<mat>(order,order),Funktionalmat);

            Q=Funktionalmat*Q;
            //Q.print("Q= ");
        } break;

    default:
        {
        // General order (e.g. 8): Gauss-Legendre nodes/weights computed on the
        // fly, then the same construction as case 5 (diagonal mass matrix from
        // the weights, stiffness Q = Funktional * D^T M). Mirrors case 5 exactly
        // but parametrized by `order`.
        mat ui(2,order,fill::zeros);

        vec Stuetz, alpha;
        GaussLegendre(order, Stuetz, alpha);   // nodes & weights on [-1,1]

        for (uword i=0; i<N; i++) {
            vec xi=0.5*(x[i]*ones(order)+x[i+1]*ones(order) + dx * Stuetz);
            xnodes=join_cols(xnodes,xi);
            InitialBoundaryObj.U_init(ui,order); //Initialisierung mit 0
            U0.cols(i*order,(i+1)*order-1)=ui;
        }

        // Diagonal mass matrix: each node's weight is shared by both fields (p,u).
        vec Mdiag=kron(alpha,ones<vec>(2));
        mat M=diagmat(Mdiag);
        invMdiag=repmat(1.0/Mdiag, N, 1);     // diagonal of kron(I_N, inv(M))

        // Stiffness matrix Q = Funktionalmat * D^T * M, kron'd to the 2-field system.
        mat DT=TransposeDiffMatrix(order,Stuetz);
        DT=kron(DT,eye<mat>(2,2));
        Q=DT*M;

        mat Funktionalmat={{0 ,  Z_0*c_0},{pow(rho_0,-1), 0}};
        Funktionalmat=kron(eye<mat>(order,order),Funktionalmat);
        Q=Funktionalmat*Q;
        } break;

    }
    }    //delete InitialBoundaryObj and other Objects

    //U0.save("U0.dat",raw_ascii);
    U0=vectorise(U0);
    //U0=zeros<vec>(U0.size()); // zu Nullsetzen von U0

    // Define ODE
    // Order-aware CFL: high-order DG needs dt ~ dx/(c*(2p+1)) with p = order-1,
    // so the stable timestep shrinks like 1/(2*order-1). Folding that in here
    // makes CFLReserve an order-independent safety fraction of the CFL limit.
    const double deltaT = CFLReserve * dx / (abs(lambda_1) * (2.0*order - 1.0));
    //U0.print("U0=");
    //invMdiag.print("invMdiag=");
    //cout<<t1<<endl;
    ClassdXdt dXdtObj(N,order,dx,Aplus,Aminus,Z_0,omega,Q,invMdiag,deltaT, Jac,U0,OpenMPMinCells,OpenMPThreads); //Initialization Objekt FundamentalMatrix
    vec X=U0;
  //dXdtObj.FluidMatrix(U0, dXdt0, t1);
    double t= t0;
    // Integrate. Each step's pressure field is stored as one column of a
    // preallocated buffer (contiguous in Armadillo's column-major layout);
    // growing the result with join_vert would copy everything every step.
    using StepFn = void (ClassdXdt::*)(vec &, double &);
    StepFn step = nullptr;
    if      (TimeIntegration=="EulerExplicit")          step = &ClassdXdt::EulerExplicitClassic;
    else if (TimeIntegration=="RungeKuttaClassic")      step = &ClassdXdt::RungeKuttaIntegrationClassic;
    else if (TimeIntegration=="RungeKuttaSecond")       step = &ClassdXdt::RungeKuttaIntegrationSecondVersion;
    else if (TimeIntegration=="RungeKuttaSecondOrder")  step = &ClassdXdt::RungeKuttaSecondOrderHeun;
    else if (TimeIntegration=="PredictorCorrectorHeun") step = &ClassdXdt::PredictorCorrectorHeun;
    else {
        cout << "The TimeIntegration variable isn't defined correctly!" << endl;
        exit(EXIT_FAILURE);
    }
    const bool printEachStep = (TimeIntegration=="EulerExplicit");

    const uvec pressureDofs = regspace<uvec>(0, 2, X.n_elem - 2);
    // t advances by deltaT per step (accumulated in floating point), so the
    // count is only estimated; the buffers grow if it falls short.
    uword capacity = static_cast<uword>(std::ceil(std::max(0.0, (t1 - t0) / deltaT))) + 2;
    mat snapshots(pressureDofs.n_elem, capacity);
    vec TimeSteps(capacity);
    uword nSnapshots = 0;
    auto storeSnapshot = [&]() {
        if (nSnapshots == capacity) {
            capacity *= 2;
            snapshots.resize(snapshots.n_rows, capacity);
            TimeSteps.resize(capacity);
        }
        snapshots.col(nSnapshots) = X.elem(pressureDofs);
        TimeSteps[nSnapshots] = t;
        ++nSnapshots;
    };

    storeSnapshot();   // initial state at t0
    do {
        (dXdtObj.*step)(X, t);
        storeSnapshot();
        if (printEachStep) cout << "t=" << t << endl;
    } while (t < t1);

    TimeSteps.resize(nSnapshots);
    const mat pressureSolution = trans(snapshots.cols(0, nSnapshots - 1));  // rows = time steps
    snapshots.reset();

    //X.save("X.dat",raw_ascii);
    //pressureSolution.print("Pressure solution =");
    //pressureSolution.save("X.dat",raw_ascii);
    //TimeSteps.save("time.dat",raw_ascii);

    EndTimeCalc = chrono::system_clock::now();


    double elapsed_time =  chrono::duration_cast<chrono::microseconds>
                             (EndTimeCalc-StartTime).count();



    cout << "The calculations are finished!" << endl;
    cout << "elapsed time: " << elapsed_time / 1000.0 /1000.0 << "s\n";
    
    if (!EnablePlotting) {
        cout << "Plotting disabled by config." << endl;
        return 0;
    }

    cout << "Plotting starts!" << endl;

    ClassPlot PlotObj(pressureSolution, TimeSteps, xnodes, x0, x1);

    PlotObj.RenderVideo("output.mp4");

    EndTimePlot = chrono::system_clock::now();
    elapsed_time =  chrono::duration_cast<chrono::microseconds>
                             (EndTimePlot-EndTimeCalc).count();
    cout << "Plotting has finished!" <<endl;
    cout << "elapsed time: " << elapsed_time / 1000.0 /1000.0 << "s\n";

    cout << "Press enter to exit." << endl;
    // Drop input typed ahead (e.g. a habitual Enter after the menu key) so it
    // does not answer this prompt.
    if (isatty(STDIN_FILENO)) tcflush(STDIN_FILENO, TCIFLUSH);
	cin.get();

    return 0;
}
