# Direktes Video-Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the gnuplot PNG pipeline with in-process frame rendering piped as raw RGB into ffmpeg, encoding every time step at 60 fps.

**Architecture:** A new pure `FrameRenderer` (no I/O) draws one 800×600 RGB frame from (x, p) arrays onto a pre-rendered background (box, dashed grid, tick labels from an embedded bitmap font). `ClassPlot` renders frames in parallel batches with `std::thread` and writes them in order into one `popen("ffmpeg … -i - …", "w")`. gnuplot and the `*.png` frames disappear.

**Tech Stack:** C++14, Armadillo, std::thread, ffmpeg/libx264 (external process), bash test scripts, Makefile.

**Spec:** `docs/superpowers/specs/2026-11-24-direktes-video-rendering-design.md`

## Global Constraints

- C++14 (`-std=c++14`), `-Wall` clean, no new library dependency (font is embedded).
- Frame: 800×600, `rgb24`, white background, black box, grey dashed grid at ticks, numeric tick labels on x and y, violet curve `(148,0,211)` with `+` markers at the nodes, antialiased (Wu) line. No legend.
- Axes: x from config `x0` to `x1`; y fixed `[-2, 2]`; values outside are clipped.
- Every stored time step becomes one video frame; output 60 fps.
- ffmpeg command: `ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s 800x600 -framerate 60 -i - -c:v libx264 -preset veryfast -pix_fmt yuv420p output.mp4`.
- ffmpeg missing or aborting: print a message containing `Video encoding failed`, no crash (SIGPIPE ignored while writing), no hang, program continues to the exit prompt.
- No PNG files are written anymore.
- Program output stays English; README stays German.

## Review Focus

1. **Diverging solution (EulerExplicit at default CFL) yields `inf`/`NaN`/huge values** → the frame is drawn without out-of-bounds writes; non-finite points are skipped, huge ones clipped. Test: Task 1 `test_nonfinite_and_huge_values`.
2. **ffmpeg not installed or exits immediately** → `Video encoding failed …` printed, exit code 0, no SIGPIPE death, finishes within the timeout. Test: Task 2 `test_ffmpeg_failure_reported`.
3. **Default run has 12002 frames (8.6 GB raw)** → frames must be streamed in bounded batches, never buffered all at once; peak RSS stays far below 1 GB. Test: Task 2 `test_memory_bounded` (macOS only, `/usr/bin/time -l`).
4. **Config with a different domain (e.g. `x1: 2.0` or `x1: 0.37`)** → 4–11 readable x ticks, not dozens or none. Test: Task 1 `test_nice_ticks`.
5. **Very short run (a handful of time steps)** → still a valid video with exactly that many frames. Test: Task 2 `test_frame_count_matches` uses the small smoke config.

---

### Task 1: FrameRenderer

**Files:**
- Create: `include/FrameRenderer.hpp`, `src/FrameRenderer.cpp`
- Create: `tests/test_frame_renderer.cpp` (plain `assert`-style checks, prints `PASS <name>`, exits non-zero on failure)
- Modify: `Makefile` (add `obj/Release/FrameRenderer.o` to `OBJS` + its rule; new phony target `test-render` building `bin/Release/test_frame_renderer` from the test + `FrameRenderer.o` and running it)

**Interfaces:**
- Produces:
  ```cpp
  std::vector<double> niceTicks(double lo, double hi, int maxTicks);
  class FrameRenderer {
  public:
      static constexpr int kWidth = 800, kHeight = 600;
      FrameRenderer(double xmin, double xmax, double ymin, double ymax);
      std::size_t frameBytes() const;                       // kWidth*kHeight*3
      const std::vector<std::uint8_t> &background() const;  // rgb24, row-major, top row first
      void render(const double *x, const double *y, std::size_t n, std::uint8_t *out) const;
      int plotLeft() const; int plotRight() const; int plotTop() const; int plotBottom() const;  // box edges in px
      double toPixelX(double x) const; double toPixelY(double y) const;
  };
  ```
- Plot box edges (match the current gnuplot frame): left 60, right 772, top 15, bottom 568.
- `niceTicks`: step from {1, 2, 2.5, 5}·10ᵏ, smallest step giving ≤ `maxTicks` ticks; ticks are the multiples of the step inside `[lo, hi]` (with a 1e-9·range tolerance). `lo == hi` returns `{lo}`. Labels formatted with `%g`. Use `maxTicks = 11` for x and y.
- `render`: copy `background()` into `out`, draw segments between consecutive finite points, then `+` markers (arm length 3 px). All drawing clipped to the plot box interior. Font: 5×7 glyphs for `0-9 . -`, drawn at scale 2.

- [ ] **Step 1: Write the failing tests** in `tests/test_frame_renderer.cpp`:

```cpp
// test_nice_ticks
assert(niceTicks(0.0, 0.5, 11) == (std::vector<double>{0,0.1,0.2,0.3,0.4,0.5}));   // approx-equal, 1e-12
assert(niceTicks(-2.0, 2.0, 11) == step 0.5 from -2 to 2 (9 ticks));
auto t2 = niceTicks(0.0, 2.0, 11);   assert(t2.size() >= 4 && t2.size() <= 11);
auto t3 = niceTicks(0.0, 0.37, 11);  assert(t3.size() >= 4 && t3.size() <= 11);
assert(niceTicks(1.0, 1.0, 11).size() == 1);

// test_background_layout   (FrameRenderer r(0, 0.5, -2, 2))
pixel(bg, r.plotLeft(), 300) == black;          pixel(bg, 30, 300) is not inside box → white unless label
pixel(bg, 100, 100) == white;                   // interior, off-grid
some pixel on column round(r.toPixelX(0.1)) between top and bottom is grid grey (160,160,160)
some non-white pixel in the label band below the box near x = r.toPixelX(0.3)  (tick label drawn)

// test_render_curve
x = {0, 0.25, 0.5}, y = {0, 0, 0}: pixel at (round(toPixelX(0.125)), round(toPixelY(0))) has red>green and blue>green (violet)
n == 0: out == background()

// test_nonfinite_and_huge_values
y = {NaN, Inf, -Inf, 1e300, -1e300, 0} on 6 x values: render() returns, and every pixel outside the box
interior (x <= plotLeft || x >= plotRight || y <= plotTop || y >= plotBottom) equals background()
```

- [ ] **Step 2: Run `make test-render`** — Expected: compile error (FrameRenderer missing).
- [ ] **Step 3: Implement `include/FrameRenderer.hpp` / `src/FrameRenderer.cpp`** per the Interfaces block; dashed grid = 4 px on / 4 px off, ticks drawn inward 6 px on all four sides like gnuplot.
- [ ] **Step 4: Run `make test-render`** — Expected: four `PASS` lines, exit 0. Run `make Release` — no warnings.
- [ ] **Step 5: Commit** `feat: add in-process frame renderer for the pressure video`

---

### Task 2: Stream frames into ffmpeg, drop gnuplot

**Files:**
- Modify: `include/ClassPlot.hpp`, `src/ClassPlot.cpp` (rewrite), `src/main.cpp:561-575` (plot call site)
- Create: `tests/run-video-render.sh`
- Modify: `tests/run-linux-smoke.sh`, `Dockerfile.linux-arm64` (drop `gnuplot-nox`), `Makefile` (drop `video` target and `VIDEO`/`PLAYER` vars, fix gnuplot comment; keep `*.png` in `clean`), `README.md` (all gnuplot/PNG mentions: lines ~5, 48, 57, 60, 96, 102, 109, 113-116, 133, 159; new runtime numbers)

**Interfaces:**
- Consumes: `FrameRenderer` (Task 1).
- Produces:
  ```cpp
  ClassPlot(const mat &pressure, const vec &TimeSteps, const vec &xnodes, double x0, double x1);
  bool RenderVideo(const std::string &filename);   // true on success
  ```
  `Plot()` and `MakeVideo()` are removed. `main.cpp` calls `RenderVideo("output.mp4")` with `x0`, `x1` from the config; keeps "Plotting starts!" / "Plotting has finished!" / elapsed-time lines.
- `RenderVideo` prints exactly `Rendering <n> frames at 60 fps` first, then `Video written to <file>` on success, or `Video encoding failed (ffmpeg returned <code>). Is ffmpeg on the PATH?` on failure. Batch = `4 × hardware_concurrency()` frames rendered in parallel into a reusable buffer, then `fwrite` in order. Ignore SIGPIPE (save/restore the old handler) around the write loop; stop on the first short write; always `pclose`.

- [ ] **Step 1: Write the failing test script** `tests/run-video-render.sh` (style of `run-integrator-menu.sh`: `make Release`, temp work dir, `pass`/`fail`, failure count):

```bash
test_frame_count_matches   # tests/linux-smoke-config.yaml, </dev/null, timeout 60
  n from "Rendering <n> frames at 60 fps"; output.mp4 non-empty;
  ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames,r_frame_rate,width,height -of csv=p=0
  == "800,600,60/1,<n>" (field order as ffprobe prints it); no *.png in the work dir
test_ffmpeg_failure_reported   # PATH="$fake:$PATH" where $fake/ffmpeg is `#!/bin/sh\nexit 1`
  timeout 60, exit code 0, output contains 'Video encoding failed', contains 'Plotting has finished!'
test_memory_bounded            # Darwin only (skip with PASS-skip message elsewhere); default config.yaml
  /usr/bin/time -l, "maximum resident set size" < 1073741824
```

- [ ] **Step 2: Run `bash tests/run-video-render.sh`** — Expected: FAIL (no "Rendering … frames" line; PNGs present).
- [ ] **Step 3: Implement `ClassPlot::RenderVideo`** and the `main.cpp` call site; delete the gnuplot script code.
- [ ] **Step 4: Run `bash tests/run-video-render.sh` and `bash tests/run-integrator-menu.sh`** — Expected: both end with their all-passed line.
- [ ] **Step 5: Visual check** — run default config, extract frame 8150 (`ffmpeg -i output.mp4 -vf "select=eq(n\,8150)" -frames:v 1 f.png`) and compare against the gnuplot frame `00008150.png` from the old build: same curve shape and position, same axes and tick values.
- [ ] **Step 6: Clean up dependents** — smoke test checks `ffprobe … nb_read_frames ≥ 2` instead of PNG count (message `… passed with N frames.` kept); Dockerfile, Makefile, README as listed under Files; README runtime from a measured default run.
- [ ] **Step 7: Commit** `feat: render the video in-process and stream it to ffmpeg`
