# Discontinuous Galerkin — 1D Akustik-Wellengleichung

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![CI](https://github.com/gebab88/Wave_Propagation_w_DG/actions/workflows/ci.yml/badge.svg)](https://github.com/gebab88/Wave_Propagation_w_DG/actions/workflows/ci.yml)

Ein 1D-Löser für die lineare **akustische Wellengleichung** mit der **Discontinuous-Galerkin-Methode** (DG) in C++. Eine von links einlaufende Sinuswelle breitet sich durch ein Rohr aus und wird an der starren rechten Wand reflektiert; das Ergebnis wird direkt im Programm als Bildfolge gezeichnet und ohne Zwischendateien an ffmpeg zu einem Video kodiert.

![Stehende Welle im eingeschwungenen Zustand (Druck p über x)](docs/standing-wave.png)

---

## Physikalisches Modell

Gelöst wird das lineare akustische System für Druck `p` und Schnelle `u`:

```
∂/∂t (p, u) + A ∂/∂x (p, u) = 0,     A = [[0, K], [1/ρ₀, 0]]
```

mit Kompressionsmodul `K = Z₀·c₀ = ρ₀·c₀²` und Impedanz `Z₀ = ρ₀·c₀`. Die Eigenwerte von `A` sind `±c₀` (rechts-/linkslaufende Wellen).

- **Diskretisierung:** nodale DG-Methode mit Gauß-Legendre-Knoten pro Zelle, Upwind-Fluss (charakteristische Aufspaltung `A = A⁺ + A⁻`).
- **Linker Rand:** einlaufende Sinuswelle (reine rechtslaufende Charakteristik, `freq_i = 3 kHz`).
- **Rechter Rand:** starre Wand (Reflexion, Druckverdopplung → Amplitude ±2 im eingeschwungenen Zustand).
- **Zeitintegration:** standardmäßig klassisches Runge-Kutta 4. Ordnung (RK4); weitere Verfahren sind verfügbar (siehe Konfiguration), darunter der deutlich langsamere Prädiktor-Korrektor nach Heun (implizite Trapezregel via Fixpunktiteration).

Ausführliche technische Dokumentation mit Herleitung, Stabilitätsgrenzen und gemessenen Konvergenzordnungen:
[DG-Raumdiskretisierung](docs/discontinuous-galerkin.md) · [Zeitintegration](docs/zeitintegration.md)

---

## Projektstruktur

| Datei | Zweck |
|---|---|
| `src/main.cpp` | Aufbau (Anfangswerte, Massen-/Steifigkeitsmatrix), Zeitschleife, Orchestrierung |
| `config_example.yaml` | Versionierte Vorlage mit allen Simulationsparametern und Defaults |
| `config.yaml` | Eigene, lokale Einstellungen (nicht im Git; zur **Laufzeit** via yaml-cpp gelesen) |
| `src/ClassdXdt.{cpp,hpp}` | Räumlicher DG-Operator (rechte Seite der ODE, numerischer Fluss) + Zeitintegratoren |
| `src/InitialAndBoundary.{cpp,hpp}` | Anfangs- und Randbedingungen |
| `src/vandermonde.{cpp,hpp}` | Vandermonde- & Differentiationsmatrix (für die Steifigkeitsmatrix) |
| `src/FrameSampler.{cpp,hpp}` | Wählt die Video-Frames: feste Zeitabstände, linear zwischen den Rechenschritten interpoliert |
| `src/SystemMemory.{cpp,hpp}` | Fragt den eingebauten RAM ab (Grenze für den Frame-Puffer) |
| `src/FrameRenderer.{cpp,hpp}` | Zeichnet die Video-Frames: Achsen, Gitter, Kurve |
| `src/ClassPlot.{cpp,hpp}` | Paralleles Zeichnen der Frames, Streaming der rohen Frames nach ffmpeg |
| `src/VideoPipe.{cpp,hpp}` | Pipe zu ffmpeg: Starten, Quoting, Exit-Status, je eine Variante für POSIX und Windows |
| `Makefile` | Build (für macOS / MacPorts konfiguriert) |

---

## Abhängigkeiten (macOS / MacPorts)

Das Projekt ist für **macOS mit MacPorts** und Apple `clang++` eingerichtet (BLAS/LAPACK über das **Accelerate**-Framework).

```sh
sudo port install armadillo
sudo port install libomp                                                   # OpenMP
sudo port install yaml-cpp                                                 # Laufzeit-Config (config.yaml)
sudo port install ffmpeg                                                   # für die Video-Erzeugung
```

| Bibliothek | Verwendung | Hinweis |
|---|---|---|
| Armadillo | Lineare Algebra | nur Header (`-DARMA_DONT_USE_WRAPPER`) + Accelerate |
| yaml-cpp | Laufzeit-Config | parst `config.yaml`; gelinkt mit `-lyaml-cpp` |
| libomp | OpenMP (Zellschleife) | Apple clang braucht `-Xpreprocessor -fopenmp` |
| ffmpeg | Frames → `output.mp4` | wird zur Laufzeit per `popen` gestartet; die Frames kommen als rohes rgb24 über stdin |
| Accelerate | BLAS/LAPACK | macOS-Framework, kein extra Paket |

> **Andere Plattform / andere Versionen:** Die Pfade stehen oben im `Makefile` (`MACPORTS`, `OMPFLAGS`, `OMPLIBS`). Unter Linux Accelerate durch `-lopenblas -llapack` ersetzen.

---

## Bauen

```sh
make Release          # -> bin/Release/DiscontinousGalerkin (legt config.yaml aus config_example.yaml an, falls sie fehlt)
make Debug            # -> bin/Debug/DiscontinousGalerkin, ohne Optimierung und mit Debug-Infos
make test             # alle Tests, siehe unten
make clean            # Objektdateien, Binaries, *.png, *.mp4, *.dat entfernen
```

Simulationsparameter (Ordnung, `N`, Zeitintegrator, Fluid/Gebiet) werden **nicht** mehr zur Compile-Zeit gesetzt, sondern zur Laufzeit aus `config.yaml` gelesen — dafür ist **kein** Neukompilieren nötig (siehe [Konfiguration](#konfiguration-configyaml)). Zur Compile-Zeit lassen sich nur noch die Compiler-Flags wählen:

| Variable | Default | Bedeutung |
|---|---|---|
| `DEBUGOROPTI` | `-O3` | Flags für `make Release` |
| `DEBUG_FLAGS` | `-g -O0` | Flags für `make Debug` |

Release und Debug bauen in getrennte Objektverzeichnisse (`obj/Release`, `obj/Debug`). Ein Wechsel zwischen beiden erzwingt also keinen Neubau.

### Tests

`make test` startet alle Testpakete nacheinander. Auch wenn eines fehlschlägt, laufen die übrigen weiter, und am Ende steht, welche fehlgeschlagen sind. Einzeln:

| Target | Datei | Prüft |
|---|---|---|
| `make test-render` | `tests/test_frame_renderer.cpp` | Frame-Renderer (C++-Unit-Tests) |
| `make test-pipe` | `tests/test_video_pipe.cpp` | Pipe zu ffmpeg: Bytes kommen unverändert an, Exit-Status (C++-Unit-Tests) |
| `make test-sampler` | `tests/test_frame_sampler.cpp` | Frame-Sampler: Zeitpunkte, Interpolation, Grenzfälle, Speicherbedarf, RAM-Abfrage (C++-Unit-Tests) |
| `make test-input-checks` | `tests/run-input-checks.sh` | Eingabeprüfung und Abbruch instabiler Rechnungen |
| `make test-menu` | `tests/run-integrator-menu.sh` | Integrator-Menü im Pseudo-Terminal |
| `make test-video` | `tests/run-video-render.sh` | Video-Ausgabe, ffmpeg-Fehler, Speicherbedarf, Speicherfehler (nur Linux) |

Die Skripte bauen `bin/Release` selbst. Den Speicherbedarf misst `/usr/bin/time`; unter Linux kommt es aus dem Paket `time`. Die meisten Menü-Tests brauchen ein Pseudo-Terminal von `script`. Bekommen sie keines, werden diese 16 Tests übersprungen; in der CI (Variable `CI` gesetzt) schlagen sie dann fehl. `test_ctrl_z_uses_config` wird unter macOS und Linux immer übersprungen, weil Strg+Z dort den Job anhält. Ebenso `test_ctrl_c_ends_windows_menu`: Es prüft Strg+C unter Windows, unter macOS und Linux tut das `test_ctrl_c_restores_terminal`. Unter Windows laufen die Menü-Tests über `tests/conpty_run.cpp` (ConPTY) statt über `script`; dort entfallen `test_memory_bounded`, `test_memory_without_plotting`, `test_ctrl_c_restores_terminal`, `test_ignored_sigint_stays_ignored` und `test_background_job_skips_menu`, weil sie POSIX-Verhalten oder `time(1)` voraussetzen.

GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) baut bei jedem Push und Pull Request `Release` und `Debug` und führt `make test` aus: unter Ubuntu 24.04 auf x86_64 und auf arm64 sowie unter Windows 11 mit MSYS2 auf x64 (`windows-2025`, UCRT64) und auf ARM64 (`windows-11-arm`, CLANGARM64). Die Windows-Jobs starten das fertige Programm außerdem aus PowerShell, also außerhalb von MSYS2.

### Linux via Docker testen (arm64 und x64)

Der macOS-Build nutzt weiterhin Accelerate/MacPorts. Auf Linux schaltet der
`Makefile` automatisch auf `g++`, OpenBLAS/LAPACK, yaml-cpp und `-fopenmp` um.
Für schnelle Linux-Tests gibt es zwei Docker-Targets. Sie nutzen Ubuntu 24.04,
`UBUNTU=<version>` wählt eine andere Version:

```sh
# falls Colima verwendet wird:
colima start --arch aarch64

make docker-test-linux-arm64              # linux/arm64, auf Apple Silicon nativ
make docker-test-linux-x64                # linux/amd64, auf Apple Silicon emuliert
make docker-test-linux-x64 UBUNTU=26.04   # dasselbe mit Ubuntu 26.04
```

Beide bauen aus `Dockerfile.linux` ein Image für die jeweilige Plattform,
kompilieren das Projekt, führen `make test-render` aus und starten eine kleine
Simulation mit `tests/linux-smoke-config.yaml`. Dabei werden Config-Parsing,
generische Ordnung 3, Frame-Rendering und ffmpeg-Video-Encoding geprüft. Läuft
der Container nicht auf der gewünschten Architektur, bricht der Smoke-Test ab.
Die lokale `config.yaml` kommt nicht ins Image; der Build legt sie aus
`config_example.yaml` an.

Das Image heißt `dg-linux-<arch>-test:<version>`. Darin lassen sich die volle
Testsuite oder eine Shell starten:

```sh
docker run --rm --platform linux/amd64 dg-linux-x64-test:24.04 make test
docker run --rm --platform linux/arm64 dg-linux-arm64-test:24.04 make test
docker run --rm -it --platform linux/amd64 dg-linux-x64-test:24.04 bash
```

Auf Apple Silicon emuliert colima x64 über QEMU (`binfmt: true` in der
colima-Konfiguration) oder, mit `colima start --vz-rosetta`, über Rosetta.
Getestet sind Ubuntu 24.04 und 26.04. Auf einem Apple M5 Pro mit QEMU dauert der
erste Build etwa 1½ Minuten (arm64) bzw. 3 Minuten (x64), `make test` im Image
etwa 40 Sekunden bzw. 4 bis 5 Minuten. Unter QEMU stürzt g++ vereinzelt mit
„Segmentation fault“ ab. Das liegt am Emulator, ein erneuter Aufruf baut durch.

Mit Ubuntu 26.04 überspringt `make test` im per QEMU emulierten x64-Image die
16 Menü-Tests, die ein Terminal brauchen. glibc 2.43 fragt das Terminal über das
ioctl `TCGETS2` ab, und das kennt QEMU 7.0 nicht. Dort sieht also kein Programm
ein Terminal. Nativ unter arm64 laufen mit 26.04 alle Tests.
Unter QEMU wirkt außerdem `ulimit -v` nicht: QEMU nimmt das Limit an, setzt es
aber nicht um. `test_buffer_allocation_failure_reported` wird im x64-Image
deshalb übersprungen.
Die CI testet beide Architekturen nativ ([Tests](#tests)).

### CI lokal ausführen

```sh
make ci-local                          # macOS, Linux arm64 und Linux x64
make ci-local CI_JOBS=linux-arm64      # nur ausgewählte Jobs (mac, linux-arm64, linux-x64)
```

`scripts/ci-local.sh` führt die beiden Linux-Jobs der CI in den Docker-Images
aus und unter macOS zusätzlich dieselben Schritte auf dem Mac:
`make Release Debug` und `make test`. Die Linux-Jobs setzen wie GitHub Actions
`CI=true`, Menü-Tests ohne Terminal schlagen dann fehl, statt übersprungen zu
werden. Am Ende steht eine Übersicht mit Ergebnis, Dauer und der Zahl der
bestandenen, fehlgeschlagenen und übersprungenen Tests. Die Logs liegen in
`bin/ci-local/`. Ist Docker nicht erreichbar, startet das Skript colima und
stoppt es am Ende wieder. Scheitert der Image-Build, etwa am g++-Absturz unter
QEMU, versucht es ihn ein zweites Mal. Die Windows-Jobs laufen nur in GitHub
Actions.

---

## Windows (MSYS2)

Das Projekt baut unter Windows 11 mit [MSYS2](https://www.msys2.org/), auf x64 und auf ARM64. Es nutzt dasselbe `Makefile` wie unter macOS und Linux. Auf x64 kommt GCC aus der Umgebung UCRT64, auf ARM64 clang aus CLANGARM64.

Nach der Installation von MSYS2 die passende Shell öffnen (Startmenü: „MSYS2 UCRT64“ bzw. „MSYS2 CLANGARM64“) und die Pakete installieren:

```sh
# x64, Shell "MSYS2 UCRT64"
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-armadillo mingw-w64-ucrt-x86_64-openblas mingw-w64-ucrt-x86_64-yaml-cpp mingw-w64-ucrt-x86_64-ffmpeg

# ARM64, Shell "MSYS2 CLANGARM64"
pacman -S --needed make mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-llvm-openmp mingw-w64-clang-aarch64-armadillo mingw-w64-clang-aarch64-openblas mingw-w64-clang-aarch64-yaml-cpp mingw-w64-clang-aarch64-ffmpeg
```

Das `Makefile` erkennt MSYS2 an `uname -s` und wählt den Compiler über `MSYSTEM`: `clang++` in den `CLANG*`-Umgebungen, sonst `g++`. Die OpenBLAS aus MSYS2 enthält LAPACK, ein eigenes Paket dafür braucht es nicht. Danach wie gewohnt, ebenfalls in der MSYS2-Shell:

```sh
make Release                    # -> bin/Release/DiscontinousGalerkin.exe
make test                       # alle Tests
bin/Release/DiscontinousGalerkin.exe
```

- **Starten außerhalb von MSYS2** (Windows Terminal, PowerShell, cmd): Das `bin`-Verzeichnis der Umgebung muss im `PATH` stehen, zum Beispiel `C:\msys64\ucrt64\bin` bzw. `C:\msys64\clangarm64\bin`. Dort liegen die DLLs. Die CI startet das Programm so aus PowerShell.
- **ffmpeg** muss im `PATH` liegen. Es kann aus MSYS2 stammen (siehe oben) oder aus einer anderen Quelle. Das Programm startet es über `cmd.exe`. Fehlt es, erscheint `Video encoding failed …`, und die Rechnung läuft trotzdem durch.
- **Integrator-Menü:** Es erscheint, wenn stdin eine Windows-Konsole ist, also in Windows Terminal, PowerShell und cmd. Taste `1`–`5` wählt ein Verfahren, Enter oder Strg+Z übernimmt den Wert aus der Config, Strg+C beendet das Programm. Kommt die Eingabe aus einer Pipe oder von `NUL`, wird das Menü übersprungen und der Config-Wert gilt, angezeigt durch die Zeile `Using: …`.
- **mintty** (das Standardfenster der MSYS2-Shells): Ob das Menü dort erscheint, ist nicht geprüft, denn die CI hat keine mintty-Sitzung. Erscheint es nicht, gilt der Config-Wert; das Menü bekommt man dann sicher in Windows Terminal, PowerShell oder cmd.
- **Tests:** `make test` läuft wie unter Linux. Die Menü-Tests nutzen ConPTY, die Speichertests entfallen (siehe [Tests](#tests)).

---

## Ausführen

Eine Video-Sekunde zeigt `VideoTimePerSecond` Simulationszeit: Das Programm zeichnet alle `VideoTimePerSecond/60` s Simulationszeit einen Frame (800×600, 60 fps, y-Achse fest −2…2). Die Welle läuft damit für jedes Gitter und jede Ordnung gleich schnell. Mit der Default-Konfiguration sind es 1201 Frames, also 20 s Video. Ohne Plot speichert das Programm keine Zwischenstände. Das Programm zeichnet die Frames im Speicher und streamt sie direkt in ffmpeg; es entstehen keine PNG-Dateien, nur `output.mp4` im aktuellen Verzeichnis. Am Ende wartet das Programm mit `cin.get()` auf Enter. Fehlt ffmpeg oder bricht es ab, erscheint `Video encoding failed …`, das Programm läuft aber normal weiter.

```sh
mkdir -p run && cd run
../bin/Release/DiscontinousGalerkin </dev/null   # findet config.yaml automatisch
```

Ohne Argument wird `config.yaml` zuerst im aktuellen Verzeichnis gesucht, sonst **neben der Binary** (Repo-Wurzel, zwei Ebenen über `bin/Release/`) — der `run/`-Workflow braucht daher kein Argument. Ein expliziter Pfad als erstes Argument hat Vorrang (z. B. `../bin/Release/DiscontinousGalerkin meine_config.yaml`). Das `</dev/null` überspringt den blockierenden „Press enter to exit"-Prompt. Mit `VideoTimePerSecond: 1.0e-4` statt des Defaults entstehen ~12000 Frames; Gesamtlaufzeit dann ca. **5,5 s** (Simulation ~0,7 s, Zeichnen + Video ~4,8 s). Der Test dazu: `make test-video` (die Shell-Tests brauchen `timeout` — auf aktuellem macOS in `/usr/bin`, sonst liefert MacPorts-coreutils `gtimeout` — und `ffprobe` aus dem ffmpeg-Port).

Im Terminal erscheint vor der Rechnung ein Menü zur Wahl des Zeitintegrationsverfahrens: Taste `1`–`5` wählt ein Verfahren, Enter übernimmt den Wert aus der Config. Kommt die Eingabe (stdin) aus einer Pipe oder von `/dev/null`, oder läuft das Programm als Hintergrundjob, wird das Menü übersprungen und der Config-Wert verwendet; Ausgabe-Pipes wie `| tee` zeigen das Menü weiterhin an. Die Menü-Tests laufen mit `make test-menu` ([Tests](#tests)).

---

## Konfiguration (`config.yaml`)

Alle Parameter werden zur Laufzeit aus `config.yaml` gelesen — Änderungen wirken **ohne Neukompilieren**. `config.yaml` ist lokal und nicht versioniert; die Vorlage ist `config_example.yaml` (`make Release` kopiert sie beim ersten Bauen, sonst `cp config_example.yaml config.yaml`). Neue Parameter oder geänderte Defaults gehören in `config_example.yaml`. Nur unabhängige Größen stehen in der Datei; abgeleitete (`Npoints`, `Z_0`, `omega`, `x`, `dx`, `Jac`) werden im Code berechnet.

| Parameter | Default | Bedeutung |
|---|---|---|
| `order` | `5` | Knoten pro Zelle, beliebig ≥ 1 (1 = stückweise konstant, d. h. Finite-Volumen erster Ordnung; 8 verifiziert) |
| `TimeIntegration` | `RungeKuttaClassic` | Zeitintegrationsverfahren |
| `N` | `50` | Anzahl Zellen |
| `x0, x1` | `0.0, 0.5` | Gebiet [m] |
| `t0, t1` | `0.0, 2e-2` | Zeitintervall [s] |
| `CFLReserve` | `0.45` | Sicherheitsfraktion des CFL-Limits; 0.45 reicht mit RK4 bis Ordnung 15, Ordnung 20 braucht höchstens 0.37 ([Grenzen](docs/zeitintegration.md#44-praktische-grenzen-anwachsen-am-einströmrand)) |
| `EnablePlotting` | `true` | `false` für reine Rechen-Benchmarks ohne MP4-Ausgabe |
| `VideoTimePerSecond` | `1e-3` | Simulationszeit pro Video-Sekunde [s]; Frames alle `VideoTimePerSecond/60` s, linear zwischen den Rechenschritten interpoliert |
| `OpenMPMinCells` | `50` | Zellzahl, ab der die OpenMP-Zellschleife mit `OpenMPThreads` aktiv wird (`0` erzwingt OpenMP) |
| `OpenMPThreads` | `2` | Anzahl OpenMP-Threads für mittlere Fälle |
| `OpenMPLargeMinCells` | `512` | Zellzahl, ab der `OpenMPLargeThreads` verwendet wird (`0` deaktiviert die zweite Schwelle) |
| `OpenMPLargeThreads` | `4` | Anzahl OpenMP-Threads für große Fälle |
| `c_0` | `300.0` | Schallgeschwindigkeit [m/s] |
| `rho_0` | `1.5` | Dichte [kg/m³] |
| `freq_i` | `3000.0` | Anregungsfrequenz [Hz] |
| `mu, sigma, height` | `0.1, 0.02, 1.0` | Parameter der (optionalen) Gauß-Anfangsverteilung |

Der **Zeitschritt** ist ordnungsabhängig (DG-CFL):

```
deltaT = CFLReserve · dx / (|c₀| · (2·order − 1))
```

Dadurch ist `CFLReserve` für alle Ordnungen ungefähr gleich „sicher" (die praktisch nutzbare RK4-Grenze sinkt von ≈ 1,43 bei Ordnung 2 auf ≈ 0,69 bei Ordnung 8, siehe [Zeitintegration](docs/zeitintegration.md#4-stabilität)). Höhere Ordnung ⇒ kleinerer Zeitschritt ⇒ mehr Rechenschritte; die Frame-Zahl bleibt gleich.

Verfügbare Werte für `TimeIntegration`: `EulerExplicit`, `RungeKuttaClassic`, `RungeKuttaSecond`, `RungeKuttaSecondOrder`, `PredictorCorrectorHeun`.

Unsinnige Werte weist das Programm beim Start mit `Config error: …` ab, noch vor dem Menü: `CFLReserve`, `c_0` und `rho_0` müssen positiv sein, `x1` größer als `x0` und `t1` größer als `t0`. `VideoTimePerSecond` muss positiv und endlich sein; mit Plot sind höchstens 1 000 000 Frames erlaubt, und die Frames dürfen höchstens 60 % des eingebauten RAMs belegen (4 GB, wenn sich der RAM nicht ermitteln lässt). Scheitert das Anlegen der Frames trotzdem, etwa weil andere Programme den Speicher brauchen, endet das Programm mit einer Meldung und Status 1. Diese Werte und `freq_i` dürfen außerdem nicht `.nan` oder `.inf` sein. Während der Rechnung prüft das Programm nach jedem Schritt den Druck. Ist er nicht mehr endlich oder betragsmäßig größer als 4, das Doppelte des exakten Maximums, bricht es mit einer Meldung und Status 1 ab, ohne Video. Das passiert, wenn `CFLReserve` für das gewählte Verfahren zu groß ist, beim Euler-Verfahren etwa schon mit dem Standardwert.

---

## Performance & Parallelisierung

- **`-O3`** statt `-O0`: ~2,3× auf die Simulation.
- **Block-weiser Volumenterm:** Statt der dichten Matrix `A = kron(eye(N), Q)` wird `Q` zellweise angewandt (`vectorise(Q * reshape(X, 2·order, N))`) — ~N× weniger Arbeit; als GEMM, das Accelerate bei großem N über Kerne parallelisiert.
- **Paralleles Zeichnen** (`ClassPlot`, `FrameRenderer`, `std::thread`): Frames werden in Blöcken von `4 × Kernzahl` parallel in einen wiederverwendeten Puffer gezeichnet und der Reihe nach über eine Pipe an ffmpeg geschrieben. Der Speicherbedarf bleibt unabhängig von der Frame-Zahl beschränkt. → Zeichnen + Kodieren ~4,8 s für ~12000 Frames (Lauf mit `VideoTimePerSecond: 1.0e-4`; vorher ~13 s mit gnuplot + PNGs).
- **OpenMP-Zellschleife** in `FluidMatrix`, auf dem Raspberry Pi standardmäßig aktiviert ab `N ≥ 50` mit 2 Threads und ab `N ≥ 512` mit 4 Threads. `OpenMPMinCells: 0` erzwingt OpenMP für Benchmarks. `scripts/benchmark-openmp.sh` testet mehrere `N`/Thread-Kombinationen ohne Plotting.

---

## Hinweise / Bekanntes

- Der Projektname enthält den Tippfehler „Discontinous" (statt „Discontinuous"); das Binary heißt entsprechend `DiscontinousGalerkin`.
- Alle Ordnungen ≥ 1 funktionieren: 1, 2 und 5 sind fest kodiert, alle anderen berechnen Gauß-Legendre-Knoten/-Gewichte via Golub-Welsch; Ordnung 8 ist verifiziert. Ordnung 1 ist das Upwind-Finite-Volumen-Verfahren und verschmiert die Wellenfront deutlich (siehe [DG-Dokumentation](docs/discontinuous-galerkin.md#102-ordnung-1-verschmierte-wellenfront)).
- HDF5 ist installiert, wird aber **nicht** gelinkt (alle HDF5-Aufrufe im Code sind auskommentiert).
- Frames im Frame-Index, der ein Vielfaches der Anregungsperiode ist, treffen den zeitlichen Nulldurchgang der stehenden Welle und wirken daher flach — das ist korrektes Verhalten, kein Fehler.

---

## Lizenz

Dieses Projekt steht unter der **GNU General Public License v3.0** — siehe [`LICENSE`](LICENSE).
