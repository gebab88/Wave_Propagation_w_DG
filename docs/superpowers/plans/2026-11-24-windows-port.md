# Windows-Port mit MSYS2 — Umsetzungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der Solver baut und läuft unter Windows 11 (x64 und ARM64) aus dem
Quelltext wie unter macOS und Linux, und `make test` ist dort in der CI grün.

**Architecture:** MSYS2 mit dem bestehenden Makefile (UCRT64/GCC auf x64,
CLANGARM64/clang auf ARM64). Windows-Code steckt nur in den Terminal-Funktionen
von `src/main.cpp` und im neuen Pipe-Modul `src/VideoPipe.cpp`. Die Menü-Tests
laufen unter Windows über einen ConPTY-Helfer, das Gegenstück zu `script`.

**Tech Stack:** C++14, GNU Make, Armadillo, OpenBLAS, yaml-cpp, OpenMP, ffmpeg,
bash-Testskripte, GitHub Actions mit `msys2/setup-msys2`, Win32-Konsolen-API
(`GetConsoleMode`, `_getch`, `FlushConsoleInputBuffer`, `CreatePseudoConsole`).

**Spec:** `docs/superpowers/specs/2026-11-24-windows-port-design.md`

## Global Constraints

- C++14 (`-std=c++14`), das bestehende Makefile, keine weiteren Build-Systeme.
- Bibliotheken: Armadillo, OpenBLAS/LAPACK, yaml-cpp, OpenMP, ffmpeg. Keine neuen.
- Unter macOS und Linux ändert sich nichts: alle 46 Tests bleiben grün, Ausgaben
  und Meldungen bleiben gleich.
- `#ifdef _WIN32` nur in den Terminal-Funktionen von `src/main.cpp`, in
  `src/VideoPipe.cpp` und in den Testhelfern. Kein `#ifdef` im Solver.
- MSYS2-Umgebungen: UCRT64 (x64, GCC) und CLANGARM64 (ARM64, clang).
- CI-Runner: `windows-2025` (x64) und `windows-11-arm` (ARM64), `fail-fast: false`.
- Code-Kommentare, YAML-Kommentare und Commit-Nachrichten auf Englisch, ohne
  Co-Authored-By. Doku und README auf Deutsch.
- Pushen nur `feat/windows-port`. `main` erst nach Freigabe des Nutzers.
- `config.yaml` ist lokal und ungetrackt: nicht ändern, md5 bei Branch-Wechseln
  prüfen.

## Review Focus

1. **Start außerhalb von MSYS2** (PowerShell, MSYS2-`bin` im PATH, stdin keine
   Konsole): Das Programm rechnet ohne Menü, schreibt das Video und endet.
   Test: PowerShell-Schritt in der CI (Task 4).
2. **`config.yaml` mit CRLF**, etwa nach Bearbeiten im Editor: wird gelesen wie
   mit LF. Test: `test_crlf_config_accepted` (Task 3, alle Plattformen).
3. **Pfad mit Leerzeichen** im ffmpeg-Befehl: Die Ausgabedatei entsteht unter dem
   richtigen Namen. Test: `test_quoted_path_with_space` (Task 2).
4. **ffmpeg fehlt im PATH:** Das Programm meldet einen Fehler mit Exit-Code und
   hängt nicht. Unter Windows meldet `cmd.exe` für einen unbekannten Befehl 9009,
   nicht 1 (der Entwurf ist hier ungenau). Test: `test_missing_command_reported`
   (Task 2).
5. **Funktionstasten** (F1 liefert unter Windows 0 + Code, Pfeile 0xE0 + Code):
   genau eine Meldung „Invalid key“. Test: `test_function_key_single_message`
   (Task 5, alle Plattformen) neben dem bestehenden Pfeiltasten-Test.

## Abweichungen vom Entwurf (Builder-Calls)

- **Pipe-Code in `src/VideoPipe.cpp` statt in `ClassPlot.cpp`.** Der Frame-Test
  erkennt den Textmodus nicht sicher: Nur antialiasierte Pixel mit dem Wert 0x0A
  werden verdoppelt. Das sind wenige Bytes, weniger als ein Frame
  (800 × 600 × 3 Bytes), und ffprobe zählt dieselbe Frame-Zahl. Ein eigenes
  Modul lässt sich ohne Armadillo byte-genau testen (Task 2).
- **`-D_USE_MATH_DEFINES` unter MSYS2.** `main.cpp:301` nutzt `M_PI`, und
  MinGW definiert es mit `-std=c++14` nur mit diesem Makro.
- **`.gitattributes` mit `* text=auto eol=lf`.** Git unter Windows checkt sonst
  die Shell-Skripte und das Makefile mit CRLF aus, und bash bricht ab.
- **Compilerwahl über `MSYSTEM`**, nicht über `uname -s`. `MSYSTEM` ist in jeder
  MSYS2-Shell gesetzt; die `uname -s`-Werte der Umgebungen sind unbestätigt.

---

### Task 1: Windows-CI-Gerüst und MSYS2-Zweig im Makefile

Ergebnis: `make test-render` läuft auf beiden Windows-Runnern grün.

**Files:**
- Create: `.gitattributes`
- Modify: `Makefile:3-49` (Compilerwahl, neuer Zweig vor dem `else`)
- Modify: `.github/workflows/ci.yml` (neuer Job `windows`)

**Interfaces:**
- Produces: Make-Variable `EXE` (`.exe` unter MSYS2, sonst leer);
  `BIN = bin/Release/DiscontinousGalerkin$(EXE)`,
  `DEBUG_BIN = bin/Debug/DiscontinousGalerkin$(EXE)`. Der MSYS2-Zweig nimmt in
  Task 5 das Target `conpty-run` auf. CI-Job `windows` mit Matrix-Feldern
  `msystem`, `prefix` (`ucrt64`/`clangarm64`) und Step-ID `msys2`.

- [ ] **Step 1: `.gitattributes` anlegen** mit `* text=auto eol=lf`. Prüfen:
  `git add --renormalize . && git status --short` zeigt keine geänderten
  Dateien (alles ist schon LF).

- [ ] **Step 2: CI-Job `windows` schreiben (zunächst nur `make test-render`)**

  ```yaml
  windows:
    name: Windows ${{ matrix.arch }}
    runs-on: ${{ matrix.runner }}
    timeout-minutes: 45
    strategy:
      fail-fast: false
      matrix:
        include:
          - { runner: windows-2025,   arch: x64,   msystem: UCRT64,     prefix: ucrt64,     compiler: "gcc:p" }
          - { runner: windows-11-arm, arch: arm64, msystem: CLANGARM64, prefix: clangarm64, compiler: "clang:p openmp:p" }
    defaults:
      run:
        shell: msys2 {0}
    steps:
      - uses: actions/checkout@v7
      - uses: msys2/setup-msys2@v2
        id: msys2
        with:
          msystem: ${{ matrix.msystem }}
          update: true
          install: make
          pacboy: ${{ matrix.compiler }} armadillo:p openblas:p yaml-cpp:p ffmpeg:p
      - name: Show environment
        run: uname -s; uname -m; echo "MSYSTEM=$MSYSTEM"; (g++ --version || clang++ --version) | head -1
      - name: Frame renderer test
        run: make test-render
  ```

  Kopfkommentar der Datei um die Windows-Jobs ergänzen (Englisch).

- [ ] **Step 3: Branch pushen, CI beobachten.** Erwartet: RED auf beiden
  Runnern im Schritt „Frame renderer test“ mit
  `Unsupported OS "<uname -s>"`. Die beiden `uname -s`-Werte aus „Show
  environment“ notieren. Schlägt schon `setup-msys2` auf `windows-11-arm` fehl:
  MSYS2 dort selbst installieren (Installer von repo.msys2.org, dann
  `pacman -S --noconfirm`) und als Fund im Plan vermerken.

- [ ] **Step 4: MSYS2-Zweig im Makefile**

  - Compilerwahl im `origin CXX`-Block: `clang++`, wenn `$(MSYSTEM)` mit `CLANG`
    beginnt, sonst wie bisher.
  - Neuer Zweig vor dem `else`, erkannt über
    `$(filter MINGW% UCRT64% CLANG64% CLANGARM64% MSYS%,$(UNAME_S))`.
  - Ist `MSYSTEM` keiner der Werte `UCRT64 MINGW64 CLANG64 CLANGARM64`, bricht der
    Zweig mit `$(error Build in an MSYS2 UCRT64 or CLANGARM64 shell, not '$(MSYSTEM)')`
    ab.
  - Werte:
    ```make
    CFLAGS  = $(BASE_CFLAGS) -fopenmp -D_USE_MATH_DEFINES
    LDFLAGS = -pthread -fopenmp -lopenblas -lyaml-cpp
    EXE     = .exe
    ```
  - `BIN`/`DEBUG_BIN` mit `$(EXE)`. Das Kommentar zum Zweig nennt die beiden
    Shells.

- [ ] **Step 5: Lokal prüfen:** `make clean && make Release Debug && make test`
  auf macOS ergibt 46 PASS. Push, CI: beide Windows-Jobs grün, Linux-Jobs grün.

- [ ] **Step 6: Commit**
  `ci: add Windows jobs (MSYS2) and build the frame renderer test there`

---

### Task 2: Pipe-Modul `VideoPipe` mit byte-genauem Test

Ergebnis: `make test-pipe` ist auf macOS, Linux und beiden Windows-Runnern grün.
Die Gegenprobe „`"w"` statt `"wb"`“ wurde in der CI rot gesehen.

**Files:**
- Create: `include/VideoPipe.hpp`, `src/VideoPipe.cpp`, `tests/test_video_pipe.cpp`
- Modify: `src/ClassPlot.cpp:50-125` (nutzt das Modul; `<csignal>` und
  `<sys/wait.h>` entfallen dort)
- Modify: `Makefile` (`VideoPipe.o` in `OBJS`; Target `test-pipe`; `test-pipe` in
  `TEST_SUITES` nach `test-render`)
- Modify: `.github/workflows/ci.yml` (Windows-Schritt `make test-pipe`)

**Interfaces:**
- Produces (`include/VideoPipe.hpp`):
  ```cpp
  // Shell quoting for one argument: POSIX sh single quotes ('\'' inside);
  // Windows cmd.exe double quotes (throws std::invalid_argument on '"').
  std::string shellQuote(const std::string &arg);
  // popen(command, "w") on POSIX. On Windows _popen("\"" + command + "\"", "wb"):
  // cmd.exe /c strips the first and last quote of a command that starts with a
  // quote, so the extra outer pair keeps quoted program paths intact.
  FILE *openWritePipe(const std::string &command);
  struct PipeStatus { enum Kind { Exited, Signaled, Unknown } kind; int value; };
  // Closes the pipe and decodes the status: POSIX via WIFEXITED/WTERMSIG,
  // Windows: _pclose() is the exit code (-1 -> Unknown).
  PipeStatus closeWritePipe(FILE *pipe);
  // Ignores SIGPIPE for its lifetime and restores the old handler; no-op on Windows.
  class SigpipeIgnored { public: SigpipeIgnored(); ~SigpipeIgnored(); /* non-copyable */ };
  ```
- `ClassPlot::RenderVideo` baut den Befehl mit `shellQuote(filename)` und
  erzeugt dieselben Meldungen wie bisher aus `PipeStatus`: `Exited` → „ffmpeg
  returned N). Is ffmpeg on the PATH?“, `Signaled` → „ffmpeg killed by signal N).“,
  `Unknown` → „ffmpeg failed, status N).“.

- [ ] **Step 1: Test schreiben** (`tests/test_video_pipe.cpp`, Stil wie
  `test_frame_renderer.cpp`: eigene `check()`-Funktion, Exit-Code ≠ 0 bei Fehler).
  Das Binary hat einen Kindmodus `--copy-stdin <datei>`: stdin binär
  (`_setmode(_fileno(stdin), _O_BINARY)` unter Windows) in die Datei kopieren,
  die mit `fopen(…, "wb")` geöffnet wird.
  Den eigenen Pfad liefert unter Windows `GetModuleFileNameA`, sonst `argv[0]`.
  - `test_all_bytes_arrive_unchanged`: Bytes 0..255, viermal hintereinander
    (1024 Bytes), über `openWritePipe(shellQuote(self) + " --copy-stdin " + shellQuote(out))`
    schreiben; `closeWritePipe` → `{Exited, 0}`; Dateiinhalt == gesendete Bytes.
  - `test_quoted_path_with_space`: dasselbe mit Zieldatei `"a b.bin"` im
    Arbeitsverzeichnis; die Datei heißt genau so.
  - `test_exit_code_reported`: `openWritePipe("exit 3")` → `{Exited, 3}`.
  - `test_missing_command_reported`: unter `SigpipeIgnored` 64 KiB an
    `openWritePipe("dg_no_such_command_xyz")` schreiben; `closeWritePipe` liefert
    `kind == Exited` und `value != 0` (POSIX 127, Windows 9009). Kein Absturz,
    kein Hängen.

- [ ] **Step 2: RED prüfen:** `make test-pipe` scheitert am fehlenden Header.

- [ ] **Step 3: Modul implementieren, POSIX und Windows.** Windows zunächst
  bewusst mit `_popen(command.c_str(), "w")`, also 1:1 wie POSIX, für die
  Gegenprobe; die äußeren Anführungszeichen gehören schon dazu. `ClassPlot.cpp`
  auf das Modul umstellen. Target:
  ```make
  test-pipe: obj/Release/VideoPipe.o tests/test_video_pipe.cpp $(HEADERS)
  	$(CXX) $(CFLAGS) $(DEBUGOROPTI) tests/test_video_pipe.cpp obj/Release/VideoPipe.o -o bin/Release/test_video_pipe$(EXE)
  	bin/Release/test_video_pipe$(EXE)
  ```
  `cleanBIN`/`clean` entfernen auch `bin/Release/test_video_pipe$(EXE)`.

- [ ] **Step 4: Lokal GREEN:** `make test` auf macOS: alle PASS, darunter die
  vier neuen; Videotests unverändert grün.

- [ ] **Step 5: Push, Gegenprobe:** Windows-CI `make test-pipe` muss mit
  `test_all_bytes_arrive_unchanged` rot sein (mehr als 1024 Bytes angekommen,
  weil jedes 0x0A zu 0x0D 0x0A wird). Linux grün.

- [ ] **Step 6: `"wb"` einsetzen**, mit Kommentar, warum. Push: Windows grün.

- [ ] **Step 7: Commit(s):**
  `refactor: move the ffmpeg pipe into VideoPipe and test it byte by byte` und
  `fix: open the ffmpeg pipe in binary mode on Windows`

---

### Task 3: Terminal-Funktionen in `main.cpp`, Release-Build unter Windows

Ergebnis: `make Release Debug` und `make test-input-checks` sind unter Windows
grün. macOS und Linux verhalten sich wie vorher.

**Files:**
- Modify: `src/main.cpp:14-17` (Includes), `:120-214` (Terminal-Funktionen),
  `:648-652` (Exit-Prompt)
- Modify: `tests/run-input-checks.sh` (neuer Test)
- Modify: `.github/workflows/ci.yml` (Windows: `make Release Debug`,
  `make test-input-checks`)

**Interfaces:**
- Produces (alle `static` in `main.cpp`, in einem `#ifdef _WIN32`-Block mit
  POSIX-Zweig):
  - `static bool menuCanReadKeys();` POSIX unverändert
    `isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) == getpgrp()`; Windows:
    `GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0`.
  - `static int readKeyRaw();` liefert 0–255, -1 bei EOF/Fehler, unter Windows
    zusätzlich `kExtendedKey = 256` für Tasten mit zwei Codes. POSIX-Körper
    unverändert, mit Handler `restoreTerminalOnSigint` und `g_savedTermios` im
    POSIX-Zweig.
  - `static void discardPendingInput();` POSIX:
    `if (isatty(STDIN_FILENO)) tcflush(STDIN_FILENO, TCIFLUSH);` Windows:
    `FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));`
  - `selectTimeIntegration` ruft nur diese drei Funktionen auf. Die Tastenlogik
    bleibt gemeinsam; neu ist nur `key == 0x1A` (Strg+Z) bei „Config-Wert“. Der
    Exit-Prompt ruft `discardPendingInput()` vor `cin.get()`.
- Windows-Includes nach allen anderen: `#define NOMINMAX`,
  `#define WIN32_LEAN_AND_MEAN`, `<windows.h>`, `<conio.h>`. Sonst kollidieren
  die `min`/`max`-Makros mit `std::max` und Armadillo.

- [ ] **Step 1: Test schreiben:** `test_crlf_config_accepted` in
  `run-input-checks.sh`: `write_cfg`, dann jede Zeile mit `\r` abschließen
  (`sed 's/$/\r/'`), `run_solver … 30`; erwartet rc 0 und
  „The calculations are finished!“. Auf macOS laufen lassen: Er sollte schon
  grün sein (yaml-cpp liest CRLF). Er sichert das Verhalten für Windows ab.

- [ ] **Step 2: POSIX-Refactor:** Die drei Funktionen extrahieren, die
  POSIX-Körper wörtlich übernehmen. `make test` auf macOS: alle PASS.

- [ ] **Step 3: Windows-Zweig, zunächst minimal und 1:1:** `menuCanReadKeys()`
  mit `_isatty(_fileno(stdin))`, `readKeyRaw()` als einfaches `_getch()`,
  `discardPendingInput()` leer. CI-Schritte `make Release Debug` und
  `make test-input-checks` ergänzen. Push. Erwartet: Der Build läuft durch.
  `test-input-checks` ist rot mit „did not terminate within 5 s“: `NUL` gilt für
  `_isatty` als Terminal, und `_getch` wartet auf die Konsole. Das ist die
  Gegenprobe für `GetConsoleMode`.
  - Link-Fehler zu LAPACK-Symbolen (`dgetrf_`, `dgesv_`, `dsyev_` …): Paket
    `lapack:p` in die CI und `-llapack` in `LDFLAGS` des MSYS2-Zweigs.
  - Compile-Fehler aus yaml-cpp 0.9: minimal anpassen, sodass es auch mit 0.8
    (macOS, Linux) baut. Verlangen die Header einen neueren Standard als C++14:
    anhalten und den Nutzer fragen, weil das eine Randbedingung bricht.

- [ ] **Step 4: `menuCanReadKeys()` auf `GetConsoleMode` umstellen.** Push:
  `test-input-checks` unter Windows grün.

- [ ] **Step 5: Commits:**
  `refactor: isolate the terminal calls of the integrator menu` und
  `feat: build on Windows (MSYS2) with a console check for the menu`

---

### Task 4: Video unter Windows

Ergebnis: `make test-video` ist unter Windows grün; der Speichertest ist mit
Begründung übersprungen. Ein PowerShell-Schritt startet das Programm außerhalb von
MSYS2.

**Files:**
- Modify: `tests/run-video-render.sh`
- Modify: `.github/workflows/ci.yml` (Windows: `make test-video`,
  PowerShell-Schritt)

**Interfaces:**
- Produces: Windows-Erkennung in den Testskripten:
  `is_windows=0; case "$(uname -s)" in MINGW*|UCRT64*|CLANG64*|CLANGARM64*|MSYS*) is_windows=1 ;; esac`.
  Dieselbe Zeile nutzt Task 5.

- [ ] **Step 1: Push mit dem Schritt `make test-video`, ohne Skriptänderung.**
  Erwartet RED:
  - `test_frame_count_matches`: „no 'Rendering <n> frames at 60 fps' line“, weil
    die Ausgabe CRLF hat und das Muster auf `fps$` endet.
  - `test_ffmpeg_failure_reported`: rot, weil `cmd.exe` das Shell-Skript
    `fakebin/ffmpeg` nicht findet und das echte ffmpeg nimmt.
  - Speichertest: SKIP mit der allgemeinen Begründung.

- [ ] **Step 2: Skript anpassen:**
  - Ausgaben des Solvers vor dem Auswerten mit `tr -d '\r'` filtern.
  - Unter Windows ist der Fake `fakebin/ffmpeg.cmd` mit Inhalt `@exit /b 1`.
  - Speichertest:
    `SKIP test_memory_bounded (time(1) cannot measure native Windows processes)`.
  - Push: `test-video` unter Windows grün.

- [ ] **Step 3: PowerShell-Schritt** (`shell: pwsh`, `timeout-minutes: 5`):
  PATH um `${{ steps.msys2.outputs.msys2-location }}\${{ matrix.prefix }}\bin`
  erweitern. Dann
  `$null | .\bin\Release\DiscontinousGalerkin.exe tests\linux-smoke-config.yaml`.
  Fehlschlagen, wenn der Exit-Code ≠ 0 ist, „Video written to output.mp4“ fehlt
  oder `output.mp4` leer ist. Push: grün.

- [ ] **Step 4: Commit:** `test: run the video tests on Windows`

---

### Task 5: ConPTY-Helfer, Menü-Tests unter Windows, Tasten und Exit-Prompt

Ergebnis: `make test` ist unter Windows grün, und es gibt nur diese SKIPs:
Speichertest, `test_ctrl_c_restores_terminal`, `test_ignored_sigint_stays_ignored`
und `test_background_job_skips_menu`. Unter POSIX entfällt dafür
`test_ctrl_z_uses_config`. Die Gegenproben für Pfeiltaste und Exit-Prompt wurden
rot gesehen.

**Files:**
- Create: `tests/conpty_run.cpp`
- Modify: `Makefile` (MSYS2-Zweig: Target `conpty-run`)
- Modify: `tests/run-integrator-menu.sh`
- Modify: `src/main.cpp` (Windows-Zweig von `readKeyRaw` und `discardPendingInput`)
- Modify: `.github/workflows/ci.yml` (Windows: Einzelschritte durch `make test` ersetzen)

**Interfaces:**
- Produces: `bin/Release/conpty_run.exe`, Aufruf `conpty_run <program> [args...]`.
  - Die Argumente werden mit Leerzeichen verbunden; Argumente mit Leer- oder
    Tabzeichen kommen in doppelte Anführungszeichen. Ein Argument mit `"` beendet
    den Helfer mit Exit 2 und Meldung.
  - Er startet das Programm in einer Pseudokonsole mit 200 × 50 Zeichen. Davor
    stehen `_WIN32_WINNT 0x0A00` und `NTDDI_VERSION 0x0A000006`, damit
    `CreatePseudoConsole` deklariert ist.
  - Er endet mit dem Exit-Code des Programms.
  - Ist die Umgebungsvariable `CONPTY_RUN_RAW=<datei>` gesetzt, hängt er die
    ungefilterte Ausgabe an diese Datei an (Diagnose in der CI).
  - Make-Target: `conpty-run: bin/Release/conpty_run.exe`; gebaut mit
    `$(CXX) -Wall -std=c++14 -O2`.

- [ ] **Step 1: Helfer schreiben.** Festgelegt:
  - **Eingabe-Thread:** stdin-Bytes sofort in die Eingabe-Pipe der Konsole. Bei
    EOF einmal `"\x1a\r"` (Strg+Z, Enter) schreiben, als Gegenstück zum ^D von
    `script`. Die Pipe bleibt offen.
  - **Ausgabe-Thread:** Konsolenausgabe lesen, filtern, sofort nach stdout.
  - **VT-Filter** (Zustandsautomat):
    - `ESC [ <params> <final>`: Bei `C` werden max(1, n) Leerzeichen ausgegeben,
      denn ConPTY schreibt Leerzeichenfolgen als Cursorbewegung. Bei `H` oder `f`
      mit Zeile r > aktuelle Zeile werden r − aktuell Zeilenumbrüche ausgegeben.
      Alles andere entfällt.
    - `ESC ] … BEL` oder `ESC ] … ESC \` entfällt, ebenso `ESC` plus ein Zeichen.
    - `\n` erhöht die aktuelle Zeile. Die Zählung beginnt bei 1.
  - **Ende** in dieser Reihenfolge:
    1. Auf das Programm warten.
    2. Warten, bis 300 ms ohne neue Ausgabe vergangen sind, höchstens 3 s.
    3. `ClosePseudoConsole`.
    4. Den Ausgabe-Thread bis `ERROR_BROKEN_PIPE` laufen lassen und beenden.
    5. Mit dem Exit-Code des Programms enden.
  - **Fallstrick:** Sieht das Kind die Pipes des Helfers statt der Konsole (Menü
    fehlt, „Using:“ erscheint sofort), dann `STARTF_USESTDHANDLES` mit
    NULL-Handles in `STARTUPINFOEX` setzen.

- [ ] **Step 2: `run-integrator-menu.sh` anpassen:**
  - `is_windows` wie in Task 4. Unter Windows `make Release conpty-run`.
  - `CONPTY_RUN="$repo_root/bin/Release/conpty_run"` und
    `TTY_CMD_FILE="$work_dir/tty-cmd.sh"`.
  - `run_tty` unter Windows: `printf '%q ' "$@" >"$TTY_CMD_FILE"`, dann
    `script_cmd=("$CONPTY_RUN" bash "$TTY_CMD_FILE")`. Über die Datei muss der
    Befehl nicht durch die Windows-Kommandozeile gequotet werden.
  - `expect_contains` gibt bei einem Fehler die ersten 40 Zeilen der Ausgabe aus.
    Das hilft bei der Diagnose in der CI.
  - `posix_only <test> <grund>`: Unter Windows `SKIP <test> (<grund>)`, sonst
    `pty_test <test>`.
  - `windows_only <test> <grund>`: umgekehrt.
  - Gründe:
    - `test_ctrl_c_restores_terminal`: „termios: nothing to restore on Windows“
    - `test_ignored_sigint_stays_ignored`: „POSIX signal disposition“
    - `test_background_job_skips_menu`: „no job control on Windows“
    - `test_ctrl_z_uses_config`: „Ctrl+Z suspends the job on POSIX“
  - Die Schlusszeile lautet „All integrator menu tests passed (N skipped).“
  - Neue Tests:
    - `test_ctrl_z_uses_config`: `run_tty $'\032'`, erwartet
      „Using: PredictorCorrectorHeun“.
    - `test_function_key_single_message`: `run_tty $'\033OP' 2`, erwartet genau
      eine Zeile „Invalid key, press 1-5 or Enter.“ und
      „Using: RungeKuttaClassic“.
  - Auf macOS: alle PASS, `test_ctrl_z_uses_config` als SKIP.

- [ ] **Step 3: Push mit `make test` als einzigem Testschritt unter Windows.**
  Erwartet RED, genau diese drei:
  - `test_arrow_key_single_message` und `test_function_key_single_message`:
    „got 2“, weil `_getch` den zweiten Code einzeln liefert.
  - `test_enter_after_key_does_not_skip_exit_prompt`: „exited before the input
    closed“.

  Weitere Fehler sind Helfer- oder Filterfehler: mit `CONPTY_RUN_RAW` ansehen und
  im Helfer beheben, nicht in den Tests.

  Erkennt die Terminal-Probe im Helfer kein Terminal (`test -t 0` der
  MSYS2-bash), prüft sie unter Windows mit dem Solver selbst als nativem
  Programm: `run_tty $'\r' -- "$BIN" "$CFG"` muss „Select time integration
  method:“ enthalten.

- [ ] **Step 4: `readKeyRaw()` unter Windows:** Liefert `_getch()` 0 oder 0xE0,
  liest es den zweiten Code und gibt `kExtendedKey` zurück. Push: beide
  Tastentests grün.

- [ ] **Step 5: `discardPendingInput()` unter Windows** mit
  `FlushConsoleInputBuffer`. Push: `make test` unter Windows grün, nur die oben
  genannten SKIPs. In der CI darf kein „no working terminal“-Fehler auftreten.

- [ ] **Step 6: Commits:**
  - `test: add a ConPTY helper and run the menu tests on Windows`
  - `fix: read both codes of extended keys in the Windows menu`
  - `fix: drop typed-ahead input before the exit prompt on Windows`

---

### Task 6: README, Entwurf nachziehen, Gesamtprüfung

**Files:**
- Modify: `README.md` (neuer Abschnitt „Windows (MSYS2)“ nach dem Linux-Abschnitt;
  CI-Satz im Abschnitt Tests um Windows ergänzen)
- Modify: `docs/superpowers/specs/2026-11-24-windows-port-design.md`
  (Pipe-Code in `VideoPipe.cpp`, Code 9009 statt 1, gefundene `uname -s`-Werte,
  LAPACK-Befund)

- [ ] **Step 1: README-Abschnitt:**
  - MSYS2 installieren, Shell UCRT64 bzw. CLANGARM64 öffnen.
  - Die `pacman -S`-Zeile mit den tatsächlichen Paketnamen aus der CI.
  - `make Release`, `make test`, Start von
    `bin/Release/DiscontinousGalerkin.exe`.
  - Außerhalb von MSYS2 muss das `bin`-Verzeichnis der Umgebung im PATH stehen,
    z. B. `C:\msys64\ucrt64\bin`.
  - ffmpeg muss im PATH liegen.
  - Menü: erscheint in einer Windows-Konsole (Windows Terminal, PowerShell, cmd);
    sonst gilt der Config-Wert, angezeigt durch „Using: …“.
  - mintty: Verhalten in der CI nicht prüfbar, das ehrlich so sagen.
  - Strg+Z wirkt im Menü wie Enter.

- [ ] **Step 2: Gesamtprüfung:**
  - macOS `make test`: alle PASS.
  - Docker: `make docker-test-linux-arm64` und `make docker-test-linux-x64`, je mit
    `UBUNTU=24.04` und `UBUNTU=26.04`, danach `make test` im Image. Grün. Überall
    unter POSIX gibt es den SKIP von `test_ctrl_z_uses_config`. Unter x64/26.04
    kommen 15 Terminal-SKIPs dazu: die bisherigen 14 und der neue
    Funktionstasten-Test.
  - CI auf `feat/windows-port`: alle vier Jobs grün.

- [ ] **Step 3: Commit:** `docs: describe building and running on Windows`

- [ ] **Step 4: Dem Nutzer berichten und die Freigabe für Merge in `main` und
  Push einholen** (Integration-Workflow: ff-merge, push, Branch löschen).
