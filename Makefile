#verbose with -v flag
#
UNAME_S := $(shell uname -s)

ifeq ($(origin CXX), default)
  ifeq ($(UNAME_S),Darwin)
    CXX = clang++
  else ifneq ($(filter CLANG%,$(MSYSTEM)),)
    # MSYS2 CLANG64/CLANGARM64 ship clang only (no g++).
    CXX = clang++
  else
    CXX = g++
  endif
endif

# Compiler flags of `make Release` and `make Debug`. The two builds compile into
# separate object directories (obj/Release, obj/Debug), so they never mix.
DEBUGOROPTI		?= -O3
DEBUG_FLAGS		?= -g -O0

# Simulation parameters (order, N, time integrator, fluid/domain settings) are
# now read at RUNTIME from config.yaml via yaml-cpp — they are no longer
# compile-time -D macros, so no rebuild is needed to change them.

BASE_CFLAGS = -Wall -std=c++14 -pthread -Iinclude -DARMA_DONT_USE_WRAPPER

ifeq ($(UNAME_S),Darwin)
  # MacPorts paths. Armadillo headers are under /opt/local/include.
  MACPORTS ?= /opt/local
  INCLUDES = -I$(MACPORTS)/include

  # OpenMP via MacPorts libomp (Apple clang needs -Xpreprocessor -fopenmp and the
  # versioned libomp paths). Used to parallelize the per-cell loop in ClassdXdt.
  OMPFLAGS = -Xpreprocessor -fopenmp -I$(MACPORTS)/include/libomp
  OMPLIBS  = -L$(MACPORTS)/lib/libomp -lomp

  # Armadillo 15 needs at least C++14. ARMA_DONT_USE_WRAPPER -> we link
  # BLAS/LAPACK ourselves via the Accelerate framework below.
  CFLAGS  = $(BASE_CFLAGS) -m64 $(INCLUDES) $(OMPFLAGS)

  # HDF5 isn't used (all hdf5 calls in the code are commented out), so we don't
  # link it. Boost / gnuplot-iostream were dropped too: frames are rendered in
  # process (FrameRenderer) and streamed into ffmpeg (see ClassPlot), so neither is needed.
  # yaml-cpp (MacPorts) is linked for runtime config parsing (config.yaml).
  LDFLAGS = -framework Accelerate -pthread $(OMPLIBS) -L$(MACPORTS)/lib -lyaml-cpp
else ifeq ($(UNAME_S),Linux)
  # Linux builds use the distro BLAS/LAPACK packages. In the Docker test image
  # and in CI those are OpenBLAS + LAPACK, and g++ supplies OpenMP via -fopenmp.
  CFLAGS  = $(BASE_CFLAGS) -fopenmp
  LDFLAGS = -pthread -fopenmp -lopenblas -llapack -lyaml-cpp
else ifneq ($(filter MINGW% UCRT64% CLANG64% CLANGARM64% MSYS%,$(UNAME_S)),)
  # Native Windows via MSYS2: build in the UCRT64 shell (x64, GCC) or the
  # CLANGARM64 shell (ARM64, clang). `uname -s` there reports MINGW64_NT-*,
  # CLANGARM64 included, so MSYSTEM tells the environments apart.
  ifeq ($(filter UCRT64 MINGW64 CLANG64 CLANGARM64,$(MSYSTEM)),)
    $(error Build in an MSYS2 UCRT64 or CLANGARM64 shell, not '$(MSYSTEM)')
  endif
  # -D_USE_MATH_DEFINES: M_PI and friends on the UCRT headers.
  CFLAGS  = $(BASE_CFLAGS) -fopenmp -D_USE_MATH_DEFINES
  LDFLAGS = -pthread -fopenmp -lopenblas -lyaml-cpp
  EXE     = .exe
else
  $(error Unsupported OS "$(UNAME_S)"; set CFLAGS and LDFLAGS explicitly)
endif

#LDFLAGS = -L/home/georg/Downloads/armadillo-7.900.1/libarmadillo.so
#LDFLAGS = -larmadillo -lboost_iostreams -lboost_system -lboost_filesystem  -llapack -lopenblas

#LDFLAGS = -larmadillo  -DARMA_DONT_USE_WRAPPER -llapack -lopen-blas

UNAME:=$(shell uname -a)

# Explicit object lists; the pattern rules below build each object.
# (The old `obj/Release/*.o` glob matched nothing on a clean tree and broke the build.)
OBJS= obj/Release/main.o obj/Release/InitialAndBoundary.o obj/Release/ClassdXdt.o obj/Release/vandermonde.o obj/Release/ClassPlot.o obj/Release/FrameRenderer.o obj/Release/VideoPipe.o
BIN= bin/Release/DiscontinousGalerkin$(EXE)
DEBUG_OBJS= $(OBJS:obj/Release/%=obj/Debug/%)
DEBUG_BIN= bin/Debug/DiscontinousGalerkin$(EXE)

# Rebuild objects when any header changes (the rules below don't track #includes
# otherwise, so e.g. editing include/ClassdXdt.hpp would silently not recompile).
HEADERS= $(wildcard include/*.h include/*.hpp)

# Output directories are gitignored, so make sure they exist on a fresh clone.
$(shell mkdir -p obj/Release obj/Debug bin/Release bin/Debug)

Release:  $(OBJS) | config.yaml
	$(CXX)  -o $(BIN) $(OBJS) $(LDFLAGS)

# Unoptimized build with debug information, for gdb/lldb.
Debug: $(DEBUG_OBJS) | config.yaml
	$(CXX) -o $(DEBUG_BIN) $(DEBUG_OBJS) $(LDFLAGS)

# config.yaml is local and untracked; create it from the versioned template on a
# fresh clone. An existing config.yaml is never overwritten.
config.yaml:
	cp config_example.yaml config.yaml
	@echo "Created config.yaml from config_example.yaml"

obj/Release/%.o: src/%.cpp $(HEADERS)
	$(CXX) $(CFLAGS) $(DEBUGOROPTI) -c $< -o $@

obj/Debug/%.o: src/%.cpp $(HEADERS)
	$(CXX) $(CFLAGS) $(DEBUG_FLAGS) -c $< -o $@

# Plain C++ test for the frame renderer (no Armadillo needed).
test-render: obj/Release/FrameRenderer.o tests/test_frame_renderer.cpp $(HEADERS)
	$(CXX) $(CFLAGS) $(DEBUGOROPTI) tests/test_frame_renderer.cpp obj/Release/FrameRenderer.o -o bin/Release/test_frame_renderer -pthread
	bin/Release/test_frame_renderer

# Plain C++ test for the ffmpeg pipe module (byte-exact, no Armadillo needed).
test-pipe: obj/Release/VideoPipe.o tests/test_video_pipe.cpp $(HEADERS)
	$(CXX) $(CFLAGS) $(DEBUGOROPTI) tests/test_video_pipe.cpp obj/Release/VideoPipe.o -o bin/Release/test_video_pipe$(EXE)
	bin/Release/test_video_pipe$(EXE)

# Shell test suites. Each script builds bin/Release itself and exits non-zero
# when one of its tests fails.
test-input-checks:
	bash tests/run-input-checks.sh

test-menu:
	bash tests/run-integrator-menu.sh

test-video:
	bash tests/run-video-render.sh

TEST_SUITES = test-render test-pipe test-input-checks test-menu test-video

# All test suites, one after the other even under -j (the menu tests depend on
# timing). Every suite runs even if an earlier one failed; the target fails if
# any suite did.
test:
	@failed=""; \
	for suite in $(TEST_SUITES); do \
	    $(MAKE) --no-print-directory $$suite || failed="$$failed $$suite"; \
	done; \
	if [ -n "$$failed" ]; then echo "Failed test suites:$$failed"; exit 1; fi; \
	echo "All test suites passed."

cleanOBJ:
	rm -rf $(OBJS) $(DEBUG_OBJS)

cleanBIN:
	rm -rf $(BIN) $(DEBUG_BIN) bin/Release/test_frame_renderer bin/Release/test_video_pipe$(EXE)

clean:
	rm -rf $(OBJS) $(DEBUG_OBJS)
	rm -rf $(BIN) $(DEBUG_BIN) bin/Release/test_frame_renderer bin/Release/test_video_pipe$(EXE)
	rm -rf *.png
	rm -rf *.mp4
	rm -rf *.dat

cleanPictures:
	rm -rf *.png

# Linux test images (Dockerfile.linux); building runs the tests. UBUNTU picks
# the Ubuntu release, e.g. `make docker-test-linux-x64 UBUNTU=26.04`.
UBUNTU ?= 24.04

docker-test-linux-arm64:
	scripts/docker-linux-test.sh arm64 $(UBUNTU)

docker-test-linux-x64:
	scripts/docker-linux-test.sh x64 $(UBUNTU)

.PHONY: cleanOBJ cleanBIN clean cleanPictures Release Debug test test-render test-pipe test-input-checks test-menu test-video docker-test-linux-arm64 docker-test-linux-x64
