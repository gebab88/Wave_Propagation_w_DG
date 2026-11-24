# Integrator-Auswahl beim Programmstart — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Beim Start ein nummeriertes Menü der Zeitintegrationsverfahren zeigen und per einzelnem Tastendruck (ohne Enter) wählen lassen.

**Architecture:** Statische Hilfsfunktionen in `src/main.cpp`, aufgerufen direkt nach dem Lesen von `TimeIntegration` aus der Config. Einzeltastendruck über den POSIX-`termios`-Rohmodus; ohne Vordergrund-Terminal wird der Config-Wert unverändert verwendet. Getestet wird über das echte Binary in einem per `script` erzeugten Pseudo-Terminal.

**Tech Stack:** C++14, POSIX `termios`/`unistd`/`signal`, Bash, `script(1)`, `timeout(1)`.

**Spec:** `docs/superpowers/specs/2026-11-24-integrator-auswahl-design.md`

## Global Constraints

- C++14 (`-std=c++14`), keine neuen Bibliotheken, **keine Makefile-Änderung**, kein neues Quellfile.
- Muss auf macOS (Apple clang) und Linux (g++) bauen und laufen.
- Menü-Reihenfolge fest: `1) EulerExplicit`, `2) RungeKuttaClassic`, `3) RungeKuttaSecond`, `4) RungeKuttaSecondOrder`, `5) PredictorCorrectorHeun`.
- Menütexte auf Englisch, exakt:
  - Kopfzeile: `Select time integration method:`
  - Zeile pro Verfahren: `  <n>) <Name>`, beim Config-Wert zusätzlich `   (config.yaml)` angehängt
  - Prompt (ohne Zeilenumbruch): `Press 1-5, Enter = config.yaml value: `
  - Ungültige Taste: `Invalid key, press 1-5 or Enter.`
  - Ergebnis (immer, auch ohne Terminal): `Using: <Name>`
  - Ungültiger Config-Wert (stderr, Exit 1): `Config error: unknown TimeIntegration '<Wert>'`
- `TimeIntegration` bleibt Pflichtfeld in `config.yaml`.
- Benchmark-Skript, Docker-Smoke-Test und `</dev/null`-Aufrufe laufen ohne Abfrage und ohne Verhaltensänderung (die neue Zeile `Using: …` ist erlaubt; `benchmark-openmp.sh` parst nur `^elapsed time:`).

## Review Focus

1. **Ausgabe in eine Pipe** (`./DiscontinousGalerkin | tee log`, stdin ist Terminal): Das Menü muss sichtbar sein, *bevor* auf die Taste gewartet wird → `std::cout` vor dem `read` flushen. Test: `test_prompt_flushed_when_stdout_piped` (Task 1).
2. **Pfeiltasten / Mehrbyte-Tasten** (`ESC [ A`): genau *eine* Meldung `Invalid key…`, nicht drei → nach ungültiger Taste `tcflush(STDIN_FILENO, TCIFLUSH)`. Test: `test_arrow_key_single_message` (Task 1).
3. **Ctrl-D** (im Rohmodus Byte `0x04`) und echtes EOF (`read` liefert 0): wie Enter → Config-Wert. Test: `test_ctrl_d_uses_config` (Task 1).
4. **Start als Hintergrundjob** (`… &` mit Terminal als stdin): darf nicht durch `SIGTTOU`/`SIGTTIN` stehen bleiben → nur abfragen, wenn `tcgetpgrp(STDIN_FILENO) == getpgrp()`. Test: `test_background_job_skips_menu` (Task 2).
5. **SIGINT beim Start ignoriert** (`nohup`, `trap "" INT`): Der Handler darf das Ignorieren nicht aufheben → nur installieren, wenn die vorherige Disposition nicht `SIG_IGN` war. Test: `test_ignored_sigint_stays_ignored` (Task 2).

---

### Task 1: Menü, Einzeltastendruck, Fallback ohne Terminal, frühe Validierung

**Files:**
- Modify: `src/main.cpp:1-18` (Includes: `<array>`, `<termios.h>`, `<unistd.h>`; doppeltes `<iostream>` in Zeile 5 entfernen)
- Modify: `src/main.cpp:45-58` (neue statische Funktionen nach `cfgGetOr`)
- Modify: `src/main.cpp:94` (`TimeIntegration` wird nicht-const aus Config gelesen, validiert, dann durch die Auswahl ersetzt)
- Modify: `README.md:100-110` (Abschnitt „Ausführen“: zwei Sätze zum Menü und zum Überspringen ohne Terminal)
- Create: `tests/menu-test-config.yaml`
- Create: `tests/run-integrator-menu.sh` (ausführbar)

**Interfaces:**
- Produces:
  - `static const std::array<const char*, 5> kTimeIntegrators` — Namen in der Reihenfolge aus Global Constraints.
  - `static bool isKnownTimeIntegrator(const std::string &name)`
  - `static int readKeyRaw()` — schaltet `termios` auf `~(ICANON|ECHO)`, `VMIN=1`, `VTIME=0` (`TCSANOW`), liest ein Byte, stellt den gesicherten Zustand **immer** wieder her; Rückgabe Byte `0–255` oder `-1` bei EOF/Fehler.
  - `static std::string selectTimeIntegration(const std::string &configDefault)` — gibt den gewählten Namen zurück; gibt `configDefault` ohne jede Ausgabe außer `Using:` zurück, wenn `isatty(STDIN_FILENO)` falsch ist.
  - Test-Helfer in `tests/run-integrator-menu.sh`: `run_tty <key1> [<key2> …] -- <cmd…>`

- [ ] **Step 1: Testkonfiguration anlegen**

`tests/menu-test-config.yaml` = Kopie von `tests/linux-smoke-config.yaml` mit: `TimeIntegration: PredictorCorrectorHeun`, `EnablePlotting: false`, `N: 2`, `order: 2`, `t1: 1.0e-5` (ein Zeitschritt, Laufzeit < 1 s). Kopfkommentar: wofür die Datei da ist.

- [ ] **Step 2: Testskript mit Helfer und den Fällen dieses Tasks schreiben**

`tests/run-integrator-menu.sh` (`set -euo pipefail`, Repo-Root wie in `tests/run-linux-smoke.sh` ermitteln, Binary `bin/Release/DiscontinousGalerkin`, vorher `make Release`).

`run_tty`: startet `<cmd…>` in einem Pseudo-Terminal und schickt jede Taste einzeln mit `printf` und **0,5 s Pause davor**, danach 1 s Pause. Gesamtes Kommando unter `timeout 20`. macOS: `( … ) | script -q /dev/null <cmd…>`; Linux: `script -qec "<cmd…>" /dev/null`. Ausgabe mit `tr -d '\r'` säubern. Ohne Pausen kommen die Bytes an, bevor der Rohmodus aktiv ist (vorab geprüft).

Jeder Fall ist eine Bash-Funktion, druckt `PASS <name>` oder `FAIL <name>: <Grund>` und setzt bei Fehler einen Fehlerzähler; Skript endet mit Exit ≠ 0, wenn einer fehlschlug.

| Testfunktion | Eingabe | Erwartung |
|---|---|---|
| `test_key_selects_method` | `run_tty 2 -- BIN CFG` | enthält `Select time integration method:`, `  5) PredictorCorrectorHeun   (config.yaml)`, `Using: RungeKuttaClassic`, `The calculations are finished!` |
| `test_enter_uses_config` | `run_tty $'\r' -- BIN CFG` | `Using: PredictorCorrectorHeun` |
| `test_invalid_key_reprompts` | `run_tty x 2 -- BIN CFG` | `grep -c 'Invalid key, press 1-5 or Enter.'` = 1, `Using: RungeKuttaClassic` |
| `test_arrow_key_single_message` | `run_tty $'\033[A' 2 -- BIN CFG` | `grep -c 'Invalid key…'` = 1, `Using: RungeKuttaClassic` |
| `test_ctrl_d_uses_config` | `run_tty $'\004' -- BIN CFG` | `Using: PredictorCorrectorHeun` |
| `test_no_tty_skips_menu` | `BIN CFG </dev/null` | Exit 0, **kein** `Select time integration method`, `Using: PredictorCorrectorHeun` |
| `test_invalid_config_fails_early` | Config-Kopie mit `TimeIntegration: Foo`, `BIN CFG </dev/null` | Exit 1, stderr `Config error: unknown TimeIntegration 'Foo'`, stdout enthält **kein** `Q=` (Setup für Ordnung 2 druckt `Q=` → Abbruch muss davor liegen) |
| `test_prompt_flushed_when_stdout_piped` | `run_tty -- sh -c 'BIN CFG \| cat'` ohne Tasten, `timeout 3` | Ausgabe enthält `Select time integration method:` und `Press 1-5` (ohne Flush bleibt der Puffer beim Abbruch leer) |

- [ ] **Step 3: Tests laufen lassen, Fehlschlag prüfen**

Run: `bash tests/run-integrator-menu.sh`
Expected: alle Fälle `FAIL` außer `test_no_tty_skips_menu` (scheitert ebenfalls, weil `Using:` fehlt); Exit ≠ 0.

- [ ] **Step 4: Funktionen in `src/main.cpp` implementieren und in `main()` einhängen**

- `readKeyRaw`: `tcgetattr` sichern, Rohmodus setzen, `std::cout` **vor** dem `read` flushen (Aufrufer flusht ebenfalls), `read(STDIN_FILENO, &c, 1)`, Zustand wiederherstellen.
- `selectTimeIntegration`: ohne Terminal → `Using:` drucken, `configDefault` zurück. Sonst Menü + Prompt drucken, Schleife: `'1'–'5'` → Name; `'\n'`, `'\r'`, `0x04`, `-1` → `configDefault`; sonst `tcflush(STDIN_FILENO, TCIFLUSH)`, Zeilenumbruch, Ungültig-Meldung, Prompt erneut. Nach der Wahl Zeilenumbruch und `Using: <Name>`.
- `main()` Zeile 94: Config-Wert lesen → `isKnownTimeIntegrator` prüfen (sonst Meldung auf `cerr`, `return 1`) → `const std::string TimeIntegration = selectTimeIntegration(…)`. Der `else`-Zweig am Ende der Integrator-Kette (Zeile ~439) bleibt als Absicherung.

- [ ] **Step 5: Tests laufen lassen, Erfolg prüfen**

Run: `make Release 2>&1 | grep -i warning; bash tests/run-integrator-menu.sh`
Expected: keine Warnung aus `main.cpp`; alle 8 Fälle `PASS`, Exit 0.

- [ ] **Step 6: Bestehende nicht-interaktive Abläufe prüfen**

Run: `cd "$(mktemp -d)" && <repo>/bin/Release/DiscontinousGalerkin <repo>/tests/linux-smoke-config.yaml </dev/null | grep -E '^(Using|elapsed time):'`
Expected: `Using: RungeKuttaClassic` und zwei `elapsed time:`-Zeilen, kein Menü.

- [ ] **Step 7: README ergänzen** (Abschnitt „Ausführen“): Im Terminal erscheint vor der Rechnung ein Menü (Taste `1`–`5`, Enter = Config-Wert); bei `</dev/null`, Pipes oder Hintergrundjobs wird es übersprungen.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp tests/menu-test-config.yaml tests/run-integrator-menu.sh README.md
git commit -m "feat: select time integrator interactively at startup"
```

---

### Task 2: Terminal bei Ctrl-C wiederherstellen, Hintergrundjobs überspringen

**Files:**
- Modify: `src/main.cpp` (`readKeyRaw`, `selectTimeIntegration` aus Task 1; Include `<csignal>`)
- Modify: `tests/run-integrator-menu.sh` (drei Testfälle ergänzen)

**Interfaces:**
- Consumes: `readKeyRaw()`, `selectTimeIntegration(const std::string&)`, `run_tty` aus Task 1.
- Produces: keine neuen Schnittstellen; `selectTimeIntegration` behandelt zusätzlich „nicht im Vordergrund“ wie „kein Terminal“.

- [ ] **Step 1: Testfälle ergänzen**

| Testfunktion | Eingabe | Erwartung |
|---|---|---|
| `test_ctrl_c_restores_terminal` | `run_tty $'\003' -- sh -c 'trap ":" INT; BIN CFG; echo status=$?; stty -a'` | enthält `status=130`; in der `stty -a`-Ausgabe stehen die Tokens `icanon` und `echo`, **nicht** `-icanon` / `-echo` |
| `test_ignored_sigint_stays_ignored` | `run_tty $'\003' 2 -- sh -c 'trap "" INT; BIN CFG; echo status=$?'` | `Using: RungeKuttaClassic`, `status=0` |
| `test_background_job_skips_menu` | `run_tty -- bash -c 'set -m; BIN CFG & wait $!; echo status=$?'` | innerhalb von `timeout 20` fertig, `status=0`, **kein** `Select time integration method`, `Using: PredictorCorrectorHeun` |

Vorab geprüft: Ohne Wiederherstellung meldet `test_ctrl_c_restores_terminal` tatsächlich `-icanon` (der Test erkennt den Fehler).

- [ ] **Step 2: Tests laufen lassen, Fehlschlag prüfen**

Run: `bash tests/run-integrator-menu.sh`
Expected: `FAIL test_ctrl_c_restores_terminal` (`-icanon`) und `FAIL test_background_job_skips_menu` (Timeout); die 8 Fälle aus Task 1 weiter `PASS`. `test_ignored_sigint_stays_ignored` darf bereits bestehen.

- [ ] **Step 3: Signal- und Vordergrundbehandlung implementieren**

- Gesicherten `termios`-Zustand in einer dateistatischen Variablen ablegen. Vor dem Rohmodus per `sigaction` einen `SIGINT`-Handler installieren — **nur**, wenn die bisherige Disposition nicht `SIG_IGN` ist. Der Handler ruft nur async-signal-sichere Funktionen: `tcsetattr(STDIN_FILENO, TCSANOW, &saved)`, Default-Disposition setzen, `raise(SIGINT)`. Nach dem `read` den vorherigen Handler wiederherstellen.
- `selectTimeIntegration`: zusätzlich zur `isatty`-Prüfung nur fragen, wenn `tcgetpgrp(STDIN_FILENO) == getpgrp()`.

- [ ] **Step 4: Tests laufen lassen, Erfolg prüfen**

Run: `make Release 2>&1 | grep -i warning; bash tests/run-integrator-menu.sh`
Expected: keine Warnung aus `main.cpp`; alle 11 Fälle `PASS`, Exit 0.

- [ ] **Step 5: Manuelle Probe im echten Terminal** (vom Nutzer, ein Durchlauf)

Run: `cd run && ../bin/Release/DiscontinousGalerkin`, dann Ctrl-C im Menü, danach `ls` tippen.
Expected: Programm beendet, getippte Zeichen sind sichtbar, Enter funktioniert normal.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp tests/run-integrator-menu.sh
git commit -m "fix: restore terminal on Ctrl-C and skip menu for background jobs"
```
