# Video mit fester Wiedergabegeschwindigkeit: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Das Video zeigt eine feste Simulationszeit pro Video-Sekunde (`VideoTimePerSecond`, Standard 1 ms) statt eines Frames pro Zeitschritt, und ohne Plot speichert das Programm keine Zwischenstände mehr.

**Architecture:** Eine neue Klasse `FrameSampler` legt vorab einen Puffer genau der Frame-Zahl an und füllt ihn in der Zeitschleife durch lineare Interpolation zwischen zwei Rechenschritten. `ClassPlot` rendert danach wie bisher, nur mit weniger Zeilen.

**Tech Stack:** C++14, Armadillo, yaml-cpp, bestehendes Makefile, Bash-Testskripte.

**Spec:** `docs/superpowers/specs/2026-11-25-video-output-interval-design.md`

## Global Constraints

- C++14 (`-std=c++14`), bestehendes Makefile, keine neuen Bibliotheken.
- macOS, Linux und Windows (MSYS2) wie bisher, kein neues `#ifdef`.
- Die berechnete Lösung ändert sich nicht, nur was davon gespeichert wird; die Rechnung wird nicht messbar langsamer.
- Bestehende Configs laufen ohne Änderung weiter (`VideoTimePerSecond` fehlt → `1.0e-3`).
- Die lokale `config.yaml` im Repo-Wurzelverzeichnis gehört dem Nutzer: nie ändern, löschen oder committen. Nie `make clean` (löscht *.png/*.mp4/*.dat des Nutzers); stattdessen `make cleanOBJ cleanBIN`.
- Bildrate 60 fps, Auflösung und y-Achse bleiben; alle Meldungen bleiben wortgleich, auch „Rendering <n> frames at 60 fps“ und die Instabilitätsmeldung mit „(step <n>)“.
- Getrennte Zeitmessung für Rechnen und Plotten bleibt; bei Stabilitätsabbruch entsteht kein Video.
- Alle Tests laufen über `make test`. Doku Deutsch; Code-Kommentare, YAML-Kommentare und Commits Englisch, ohne Co-Authored-By.
- Arbeit auf Branch `feat/video-output-interval`. Push nur mit Freigabe des Nutzers, nie `main`.

## Review Focus

1. **Kurze Simulation** (t1 − t0 kürzer als ein Frame-Abstand): genau ein Frame (Anfangszustand), gültiges Video, kein Fehler. → Task 2, `test_single_frame_video`.
2. **Grobes Gitter** (ein Rechenschritt länger als ein Frame-Abstand): mehrere Frames pro Schritt, keine Duplikate, Frame-Zahl stimmt. → Task 1, `test_several_frames_per_step`; Task 2, `test_frames_between_steps`.
3. **Alte Config ohne `VideoTimePerSecond`**: Standard 1 ms gilt, Frame-Zahl unabhängig von N. → Task 2, `test_frame_count_independent_of_grid` (Smoke-Config ohne den Schlüssel).
4. **t0 ≠ 0**: Frame-Zeiten zählen ab t0. → Task 1, `test_times_start_at_t0`.
5. **Ende genau bei t1 bzw. Überschießen des letzten Schritts** (aufsummiertes t): letzter Frame bei t1 wird gefüllt, kein Frame nach t1. → Task 1, `test_solver_like_loop_fills_all_frames`, `test_last_time_not_after_t1`.

---

### Task 1: `FrameSampler` mit Unit-Test

**Files:**
- Create: `include/FrameSampler.hpp`, `src/FrameSampler.cpp`
- Create: `tests/test_frame_sampler.cpp`
- Modify: `Makefile` (`OBJS`, Target `test-sampler`, `TEST_SUITES`, `.PHONY`, `cleanBIN`/`clean`)

**Interfaces:**
- Produces (in `include/FrameSampler.hpp`, `#include <armadillo>`):
  ```cpp
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
      arma::uword filled() const;            // frames filled so far
      const arma::mat &frames() const;       // frameCount rows x valuesPerFrame cols
      const arma::vec &times() const;        // times()(k) = min(t0 + k * interval, t1)
  };
  ```
- Frame-Zeiten werden als `t0 + k * interval` berechnet (nicht aufsummiert) und auf t1 begrenzt.
- `frames()` hat das Layout, das `ClassPlot` erwartet (Zeile = Frame, Spalte = Knoten), damit keine zweite Kopie entsteht.

- [ ] **Step 1: Test schreiben** (`tests/test_frame_sampler.cpp`, Stil wie `tests/test_frame_renderer.cpp`: eigenes `CHECK`, `report`, Exit-Code = Fehlerzahl > 0). Vergleiche mit Toleranz 1e-12.
  - `test_frame_count`:
    - `frameCount(0, 0.02, 1e-3/60) == 1201`
    - `frameCount(0, 8e-5, 1e-3/60) == 5`
    - `frameCount(0, 8e-5, 1.0/60) == 1`
    - `frameCount(0, 1.0, 0.25) == 5` (exaktes Vielfaches zählt)
    - `frameCount(0, 1.0, 1e-12) > FrameSampler::kMaxFrames` und endlich
  - `test_times_start_at_t0`: `FrameSampler(1.0, 2.0, 0.25, 1)`, `times()` = {1.0, 1.25, 1.5, 1.75, 2.0}.
  - `test_several_frames_per_step`: `FrameSampler(0, 1.0, 0.25, 2)`; `start({0, 10})`; `advance(0, {0, 10}, 1.0, {4, 6})` → Zeilen {0,10}, {1,9}, {2,8}, {3,7}, {4,6}; `filled() == 5`.
  - `test_solver_like_loop_fills_all_frames`: `FrameSampler(0, 1.0, 0.25, 1)`, Werte = t (linear, also exakt interpolierbar). Schleife wie im Solver: `start({0})`; `do { tPrev = t; t += 0.1; advance(tPrev, {tPrev}, t, {t}); } while (t < 1.0);` (t landet nach zehn Schritten knapp unter 1, also folgt ein elfter). Danach `filled() == 5` und `frames()(k, 0) == times()(k)`.
  - `test_last_time_not_after_t1`: für (t1, interval) ∈ {(1.0, 1.0/3), (1.0, 0.1), (0.02, 1e-3/60)}: `times()(last) <= t1`.
  - `test_advance_after_last_frame_is_noop`: nach vollständigem Füllen ändert ein weiteres `advance(...)` weder `filled()` noch `frames()`.
- [ ] **Step 2: Fehlschlag prüfen.** `make test-sampler` → Abbruch, weil `FrameSampler.hpp` fehlt (bzw. kein Target).
- [ ] **Step 3: Implementieren.** `FrameSampler` wie im Interface; Makefile:
  - `obj/Release/FrameSampler.o` in `OBJS` (`DEBUG_OBJS` wird daraus abgeleitet, Makefile:77).
  - Target nach `test-pipe`:
    ```make
    # Unit test for the frame sampler (uses Armadillo, so it links like the solver).
    test-sampler: obj/Release/FrameSampler.o tests/test_frame_sampler.cpp $(HEADERS)
    	$(CXX) $(CFLAGS) $(DEBUGOROPTI) tests/test_frame_sampler.cpp obj/Release/FrameSampler.o -o bin/Release/test_frame_sampler$(EXE) $(LDFLAGS)
    	bin/Release/test_frame_sampler$(EXE)
    ```
  - `TEST_SUITES = test-render test-pipe test-sampler test-input-checks test-menu test-video`; `test-sampler` in `.PHONY`; `bin/Release/test_frame_sampler$(EXE)` in `cleanBIN` und `clean`.
- [ ] **Step 4: Grün prüfen.** `make test-sampler` → alle PASS; `make cleanOBJ cleanBIN && make Release Debug` ohne Warnung; `make test` alle PASS.
- [ ] **Step 5: Commit** `feat: add a frame sampler for a fixed video playback speed`

---

### Task 2: Sampler in der Zeitschleife, `VideoTimePerSecond`, Tests und Doku

**Files:**
- Modify: `src/main.cpp` (Config-Lesen nach `EnablePlotting` ~375; Zeitschleife ~615–680; Plot-Aufruf ~706)
- Modify: `include/ClassPlot.hpp`, `src/ClassPlot.cpp` (nur Konstante `kFps`)
- Modify: `tests/run-input-checks.sh`, `tests/run-video-render.sh`
- Modify: `config_example.yaml`, `README.md`

**Interfaces:**
- Consumes: `FrameSampler` aus Task 1.
- Produces: `static constexpr int kFps = 60;` in `ClassPlot` (öffentlich); `ClassPlot.cpp` nutzt es für `-framerate` und die Meldung „Rendering <n> frames at 60 fps“ (Text unverändert), `main.cpp` für den Frame-Abstand.

- [ ] **Step 1: Tests schreiben.**
  - `run-input-checks.sh`: In die Basis-Config von `write_cfg` die Zeile `VideoTimePerSecond: 1.0e-3` aufnehmen (nach `EnablePlotting`). Neue Zeilen im Block der `expect_rejected`:
    ```bash
    expect_rejected test_zero_video_time_rejected      VideoTimePerSecond VideoTimePerSecond=0.0
    expect_rejected test_negative_video_time_rejected  VideoTimePerSecond VideoTimePerSecond=-1.0e-3
    expect_rejected test_nan_video_time_rejected       VideoTimePerSecond VideoTimePerSecond=.nan
    expect_rejected test_infinite_video_time_rejected  VideoTimePerSecond VideoTimePerSecond=.inf
    # 8e-5 s at 1e-12 s per video second would need 4.8e9 frames:
    expect_rejected test_too_many_frames_rejected      VideoTimePerSecond EnablePlotting=true VideoTimePerSecond=1.0e-12
    ```
    Plus `test_frame_limit_ignored_without_plotting`: `write_cfg` mit `VideoTimePerSecond=1.0e-12` (Plot aus), `run_solver … 30` → rc 0 und „The calculations are finished!“.
  - `run-video-render.sh`: Helfer, der in einer Config-Kopie einen Schlüssel ersetzt oder, falls er fehlt, anhängt. Ein zweiter Helfer liefert die maximale RSS eines Laufs über `time(1)`: die bestehende Logik aus `test_memory_bounded` (Windows-SKIP, BSD `-l`/GNU `-v`, fehlendes `/usr/bin/time` → SKIP), von beiden Speichertests genutzt. Neue Tests, jeweils mit Frame-Zahl aus „Rendering <n> frames at 60 fps“ und ffprobe-Kontrolle wie `test_frame_count_matches`:
    - `test_frame_count_independent_of_grid`: `tests/linux-smoke-config.yaml` (ohne `VideoTimePerSecond`) einmal mit `N: 4`, einmal mit `N: 8` → beide `n == 5` (8e-5 s · 60 / 1e-3 = 4,8 → 5 Frames).
    - `test_frames_between_steps`: Smoke-Config mit `VideoTimePerSecond: 1.0e-5` → `n == 481`. Dort ist ein Rechenschritt (3,75e-5 s) viel länger als ein Frame-Abstand.
    - `test_single_frame_video`: Smoke-Config mit `VideoTimePerSecond: 1.0` → `n == 1`, Video mit 1 Frame, Exit 0.
    - `test_memory_without_plotting`: `config_example.yaml` mit `EnablePlotting: false`, `N: 400`, `order: 3`, `t1: 1.0e-2` → max RSS < 128 MiB. Heute sind es etwa 520 MB, Laufzeit ~5 s auf macOS.
    - `test_memory_bounded` bekommt `VideoTimePerSecond: 1.0e-4`, damit es weiter ~12 000 Frames rendert (Grenze 1 GiB unverändert).
- [ ] **Step 2: Fehlschlag prüfen.**
  - `make test-input-checks`: die vier Werte- und der Frame-Limit-Test FAIL mit „exit code 0, expected 1“.
  - `make test-video`:
    - `test_frame_count_independent_of_grid` FAIL (heute 4 bzw. 6 Frames).
    - `test_frames_between_steps` und `test_single_frame_video` FAIL (4 Frames).
    - `test_memory_without_plotting` FAIL (~520 MB).
- [ ] **Step 3: Implementieren.**
  - Config, direkt nach `EnablePlotting`, vor dem Menü: `const double VideoTimePerSecond = cfgGetOr<double>(cfg, "VideoTimePerSecond", 1.0e-3); requirePositive("VideoTimePerSecond", VideoTimePerSecond);`. Frame-Abstand `VideoTimePerSecond / ClassPlot::kFps`. Nur mit Plot: Ist `FrameSampler::frameCount(t0, t1, Abstand) > FrameSampler::kMaxFrames`, dann Fehler mit Status 1 auf stderr:
    `Config error: VideoTimePerSecond = <wert> gives <n> video frames for t0 .. t1; at most 1000000 are allowed. Increase VideoTimePerSecond or shorten t1 - t0.`
  - Zeitschleife:
    - `snapshots`, `TimeSteps`, `capacity`, `storeSnapshot` und die `trans()`-Kopie entfallen.
    - Mit Plot: `FrameSampler` vor der Schleife anlegen, `start(X.elem(pressureDofs))`. Jeder Schritt merkt sich `tPrev` und den vorigen Druck und ruft danach `advance(tPrev, prev, t, cur)`.
    - Ohne Plot: kein Sampler, keine Kopie des Drucks pro Schritt.
    - Ein Schrittzähler ersetzt `nSnapshots - 1` in der Instabilitätsmeldung. Die Zahl bleibt dieselbe: die Anzahl der ausgeführten Schritte.
  - Plot: `ClassPlot PlotObj(sampler.frames(), sampler.times(), xnodes, x0, x1);`.
  - `config_example.yaml` nach `EnablePlotting`:
    ```yaml
    # Simulated time shown per second of video [s]. Frames are taken every
    # VideoTimePerSecond/60 s of simulated time (60 fps), so the wave moves at
    # the same speed on screen for every grid. Default if missing: 1.0e-3.
    VideoTimePerSecond: 1.0e-3
    ```
  - `README.md`:
    - „Ausführen“: Statt „pro gespeichertem Zeitschritt einen Frame“ steht dort, dass eine Video-Sekunde `VideoTimePerSecond` Simulationszeit zeigt. Mit der Default-Konfiguration sind es 1201 Frames, also 20 s Video. Die Laufzeitangabe nennt dazu, mit welchem Wert gemessen wurde.
    - Tabelle „Konfiguration“: Zeile `VideoTimePerSecond` | `1e-3` | „Simulationszeit pro Video-Sekunde [s]; Frames alle VideoTimePerSecond/60 s, linear zwischen den Rechenschritten interpoliert“.
    - „Performance“: Die Messung „~12000 Frames“ wird als Lauf mit `VideoTimePerSecond: 1.0e-4` gekennzeichnet.
    - Zusätzlicher Satz: Ohne Plot speichert das Programm keine Zwischenstände.
    - Testtabelle: `make test-sampler` ergänzen.
- [ ] **Step 4: Grün prüfen.** `make cleanOBJ cleanBIN && make Release Debug` ohne Warnung; `make test` alle PASS (SKIPs wie vorher). Gegenprobe der Lösung und der Laufzeit: Ein Lauf mit `config_example.yaml` (Plot aus) liefert vor und nach der Änderung dieselbe Ausgabe, ausgenommen die Zeile „elapsed time“. Deren Wert liegt nach der Änderung nicht höher, gemessen über drei Läufe je Stand.
- [ ] **Step 5: Commits**
  - `feat: sample video frames at a fixed playback speed` (Code und Tests)
  - `docs: describe VideoTimePerSecond`

---

### Abschluss

- [ ] Nach Freigabe des Nutzers `feat/video-output-interval` pushen; CI auf allen vier Jobs grün (Linux x86_64/arm64, Windows x64/arm64). Unter Windows sind `test_memory_without_plotting` und `test_memory_bounded` SKIP.
- [ ] Dem Nutzer berichten und die Freigabe für Merge in `main` einholen.
