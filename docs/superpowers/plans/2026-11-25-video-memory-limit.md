# Speicherdeckel für den Frame-Puffer: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mit Plot lehnt das Programm vor dem Menü jede Config ab, deren Frame-Puffer mehr als 60 % des eingebauten RAMs bräuchte, statt beim Anlegen abzustürzen.

**Architecture:** Eine kleine Funktion `installedRamBytes()` in eigener Datei fragt den RAM ab (`sysconf` bzw. `GlobalMemoryStatusEx`). `FrameSampler` bekommt zwei reine Rechenfunktionen für Bedarf und Grenze. `main.cpp` prüft beides direkt nach dem Frame-Limit.

**Tech Stack:** C++14, Armadillo, yaml-cpp, bestehendes Makefile, Bash-Testskripte.

**Spec:** `docs/superpowers/specs/2026-11-25-video-memory-limit-design.md`

## Global Constraints

- C++14 (`-std=c++14`), bestehendes Makefile, keine neuen Bibliotheken; macOS, Linux (auch Raspberry Pi) und Windows (MSYS2).
- `#ifdef _WIN32` nur in `src/SystemMemory.cpp`; kein `__APPLE__`- oder `__linux__`-Zweig.
- Grenze: **60 %** des eingebauten RAMs; ist der RAM unbekannt, **4 GB** (4.0e9 Byte). Bedarf: Frames × (N·order + 1) × 8 Byte, in `double` gerechnet. Angaben in GB (10⁹ Byte) mit einer Nachkommastelle.
- Erst das Frame-Limit (1 000 000, Meldung unverändert), dann der Speicherdeckel; ohne Plot keine der beiden Prüfungen.
- Nie ein Binary **ohne** die neue Prüfung mit einer Config starten, die Terabytes braucht: macOS würde versuchen, den Puffer anzulegen und zu füllen.
- Die lokale `config.yaml` im Repo-Wurzelverzeichnis gehört dem Nutzer: nie ändern, löschen oder committen; ebenso `tests/.run-input-checks.sh.swp`. Nie `make clean`; stattdessen `make cleanOBJ cleanBIN`.
- Alle Tests laufen über `make test`. Doku Deutsch; Code-Kommentare, YAML-Kommentare und Commits Englisch, ohne Co-Authored-By.
- Arbeit auf Branch `feat/video-memory-limit`.

## Review Focus

1. **Frame-Limit und Speicherdeckel beide überschritten:** Es kommt die bisherige Frame-Meldung, nicht die Speichermeldung. → Task 2, `test_frame_limit_checked_first`.
2. **Riesige Config ohne Plot:** läuft normal durch, weil nichts gespeichert wird. → Task 2, `test_frame_buffer_limit_ignored_without_plotting`.
3. **Überlauf bei absurden Größen** (1e6 Frames × 1e9 Werte): Der Bedarf bleibt endlich und korrekt, kein Ganzzahlüberlauf. → Task 1, `test_buffer_bytes`.
4. **RAM nicht ermittelbar:** Es gilt 4 GB, und die Meldung sagt das. → Task 1, `test_max_buffer_bytes` (Wert); Meldungstext per Review in Task 2.
5. **Kleiner Rechner** (Pi mit 4 GB, Grenze 2,4 GB): Standard-Config (2,4 MB) und alle bisherigen Video-Tests laufen weiter. → bestehende Suites in `make test`.

---

### Task 1: RAM-Abfrage und Bedarfsrechnung

**Files:**
- Create: `include/SystemMemory.hpp`, `src/SystemMemory.cpp`
- Modify: `include/FrameSampler.hpp`, `src/FrameSampler.cpp`
- Modify: `tests/test_frame_sampler.cpp`
- Modify: `Makefile` (`OBJS` Makefile:75; Target `test-sampler` Makefile:117-119)

**Interfaces:**
- Produces, `include/SystemMemory.hpp`:
  ```cpp
  // Installed physical RAM in bytes, or 0 if it cannot be determined.
  double installedRamBytes();
  ```
  POSIX: `sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)`, 0 wenn einer der Werte ≤ 0 ist. Windows: `GlobalMemoryStatusEx`, `ullTotalPhys`, 0 bei Fehler. `<windows.h>` mit `NOMINMAX` und `WIN32_LEAN_AND_MEAN` einbinden wie in `src/main.cpp:31-40`.
- Produces, neue öffentliche Member von `FrameSampler`:
  ```cpp
  static constexpr double kMaxRamFraction   = 0.6;    // share of the installed RAM the frames may use
  static constexpr double kFallbackMaxBytes = 4.0e9;  // limit when the RAM size is unknown
  // Bytes of the frame buffer: frames x (valuesPerFrame + 1) doubles (values plus frame time).
  static double bufferBytes(double frames, double valuesPerFrame);
  // kMaxRamFraction * ramBytes, or kFallbackMaxBytes when ramBytes <= 0 (unknown).
  static double maxBufferBytes(double ramBytes);
  ```
  Out-of-class-Definitionen der beiden neuen `constexpr`-Member in `src/FrameSampler.cpp`, wie bei `kMaxFrames`.

- [ ] **Step 1: Tests schreiben** in `tests/test_frame_sampler.cpp` (bestehender Stil; für große Werte relative Toleranz `fabs(a - b) <= 1e-12 * fabs(b)`), `#include "SystemMemory.hpp"`:
  - `test_buffer_bytes`:
    - `bufferBytes(1201, 250) == 2412008` (Standard-Config: 2,4 MB)
    - `bufferBytes(480001, 800000)` ≈ `480001.0 * 800001.0 * 8.0` (≈ 3,07e12)
    - `bufferBytes(1e6, 1e9)` endlich und > 8e15
  - `test_max_buffer_bytes`: `maxBufferBytes(8e9)` ≈ `4.8e9`; `maxBufferBytes(0) == 4.0e9`; `maxBufferBytes(-1) == 4.0e9`.
  - `test_installed_ram_is_plausible`: `installedRamBytes() > 1e9` und `< 1e15`.
- [ ] **Step 2: Fehlschlag prüfen.** `make test-sampler` → Abbruch beim Kompilieren (`SystemMemory.hpp` fehlt, `bufferBytes` unbekannt).
- [ ] **Step 3: Implementieren** wie in den Interfaces. Makefile: `obj/Release/SystemMemory.o` in `OBJS`; `test-sampler` hängt zusätzlich von `obj/Release/SystemMemory.o` ab und linkt es mit. Fehlt `_SC_PHYS_PAGES` unter macOS: stoppen und melden (NEEDS_CONTEXT), keinen `__APPLE__`-Zweig einbauen.
- [ ] **Step 4: Grün prüfen.** `make test-sampler` → alle PASS; `make cleanOBJ cleanBIN && make Release Debug` ohne Warnung; `make test` alle PASS.
- [ ] **Step 5: Commit** `feat: add RAM query and frame buffer size helpers`

---

### Task 2: Prüfung in `main.cpp`, Tests und Doku

**Files:**
- Modify: `src/main.cpp:381-392` (Block `if (EnablePlotting)` mit dem Frame-Limit)
- Modify: `tests/run-input-checks.sh` (bei den `VideoTimePerSecond`-Tests, ~Zeile 146-179)
- Modify: `config_example.yaml` (Kommentar zu `VideoTimePerSecond`), `README.md` (Satz zur Prüfung von `VideoTimePerSecond` im Abschnitt „Konfiguration“; Dateitabelle)

**Interfaces:**
- Consumes: `installedRamBytes()`, `FrameSampler::bufferBytes`, `FrameSampler::maxBufferBytes`, `FrameSampler::kMaxRamFraction` aus Task 1. Werte pro Frame: `N * order` (Anzahl der Druckwerte).

- [ ] **Step 1: Tests schreiben** in `tests/run-input-checks.sh`:
  ```bash
  # 480 001 frames x 800 000 values: about 3 TB, more than any RAM.
  expect_rejected test_frame_buffer_too_large_rejected "60% of the RAM" EnablePlotting=true N=100000 order=8 VideoTimePerSecond=1.0e-8
  # Too many frames and too much memory: the frame limit is reported.
  expect_rejected test_frame_limit_checked_first "video frames for t0" EnablePlotting=true N=100000 order=8 VideoTimePerSecond=1.0e-12
  ```
  Dazu `test_frame_buffer_limit_ignored_without_plotting` nach dem Muster von `test_frame_limit_ignored_without_plotting`: `write_cfg` mit `N=4000 order=4 VideoTimePerSecond=5.333e-9`, Plot aus, `run_solver … 60` → rc 0 und „The calculations are finished!“. Ein Kommentar nennt den Bedarf mit Plot: 900 057 Frames × 16 000 Werte ≈ 115 GB.
- [ ] **Step 2: Erwartetes Verhalten vor der Änderung.** Die beiden letzten Tests sichern Verhalten ab, das es schon gibt, und laufen schon jetzt grün; `make test-input-checks` zeigt das. `test_frame_buffer_too_large_rejected` **nicht** mit dem alten Binary starten (siehe Global Constraints). Sein Fehlschlag wird in Step 4 belegt.
- [ ] **Step 3: Implementieren.** Im Block `if (EnablePlotting)` nach dem Frame-Limit: Bedarf mit `bufferBytes(nFrames, N * order)`, Grenze mit `maxBufferBytes(installedRamBytes())`. Liegt der Bedarf darüber, `exit(EXIT_FAILURE)` mit dieser Meldung auf stderr (GB-Werte mit einer Nachkommastelle, Frames und Werte ganzzahlig, Prozent aus `kMaxRamFraction`):
  - RAM bekannt: `Config error: the video needs <B> GB (<F> frames x <V> values); at most <L> GB (60% of the RAM) are allowed. Increase VideoTimePerSecond, shorten t1 - t0 or reduce N or order.`
  - RAM unbekannt: wie oben, nur `(RAM size unknown)` statt `(60% of the RAM)`.

  `config_example.yaml`: Den Satz zum Frame-Limit im Kommentar ergänzen um „and the frames may use at most 60% of the installed RAM“. `README.md`: Den Satz „`VideoTimePerSecond` muss positiv und endlich sein; mit Plot sind höchstens 1 000 000 Frames erlaubt.“ ergänzen um „…, und die Frames dürfen höchstens 60 % des eingebauten RAMs belegen (4 GB, wenn sich der RAM nicht ermitteln lässt)“. In die Dateitabelle kommt eine Zeile `src/SystemMemory.{cpp,hpp}`: „Fragt den eingebauten RAM ab (Grenze für den Frame-Puffer)“.
- [ ] **Step 4: Grün prüfen.**
  - `make cleanOBJ cleanBIN && make Release Debug` ohne Warnung; `make test` alle PASS (SKIPs wie vorher).
  - Fehlschlag belegen: In der Meldung vorübergehend `60%` durch `61%` ersetzen, bauen, `make test-input-checks` → `test_frame_buffer_too_large_rejected` FAIL („no config error naming '60% of the RAM'“). Danach zurücksetzen und erneut grün prüfen.
  - Messen: `test_frame_buffer_limit_ignored_without_plotting` dauert auf macOS unter 10 s. Sonst N halbieren und den Kommentar anpassen; der Bedarf muss über 50 GB bleiben.
- [ ] **Step 5: Commits**
  - `feat: reject video frame buffers larger than 60% of the RAM` (Code und Tests)
  - `docs: describe the frame buffer memory limit`

---

### Abschluss

- [ ] Nach Freigabe des Nutzers `feat/video-memory-limit` pushen und die CI auf allen vier Jobs grün sehen. Unter Windows prüft `test_installed_ram_is_plausible` die `GlobalMemoryStatusEx`-Abfrage.
- [ ] Dem Nutzer berichten und die Freigabe für den Merge in `main` einholen.
