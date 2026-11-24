# Windows-Port mit MSYS2 — Design

## Ziel

„Es soll einfach auch unter Windows laufen!“ Gemeint ist: Wer Windows 11 nutzt,
auf x64 oder auf ARM64, baut das Programm aus dem Quelltext und startet es so wie
unter macOS (MacPorts) und Linux (apt). Die Bibliotheken kommen aus einem
Paketmanager, hier MSYS2 mit `pacman`, danach folgen `make Release` und
`make test`.

Unter Windows gilt der volle Funktionsumfang:

- `config.yaml` lesen und die Werte prüfen,
- rechnen, mit Stabilitätsprüfung,
- das Video über ffmpeg schreiben,
- in einer Windows-Konsole den Integrator über das Menü wählen,
- am Ende den Exit-Prompt anzeigen.

Die CI hält das grün. Bei jedem Push läuft sie auf `windows-2025` (x64) und auf
`windows-11-arm` (Windows 11 auf ARM64).

## Nicht-Ziele

- Kein Installer und kein fertiges Paket. Die `.exe` wird weder statisch gelinkt
  noch mit DLLs gebündelt.
- Kein Visual Studio/MSVC, kein CMake, kein vcpkg, kein Cygwin.
- Kein 32-Bit-Windows.
- Rechnung, Integratoren und Videoinhalt bleiben unverändert.

## Anti-Ziele

- Unter macOS und Linux ändert sich nichts. Alle bestehenden Tests bleiben grün,
  Ausgaben und Meldungen bleiben gleich.
- Kein `#ifdef` quer durch den Solver. Plattformcode gibt es nur im Menü (Terminal
  bzw. Konsole) und in der ffmpeg-Pipe.
- Unter Windows darf das Programm weder hängen noch die Frames verfälschen.

## Randbedingungen

- Es bleibt bei C++14, dem bestehenden Makefile und denselben Bibliotheken:
  Armadillo, OpenBLAS/LAPACK, yaml-cpp, OpenMP und ffmpeg.
- MSYS2 hat alle Pakete für UCRT64 (x64) und CLANGARM64 (ARM64), Stand 2026-11-24:

  | Paket | Version |
  |---|---|
  | Armadillo | 15.8 |
  | OpenBLAS | 0.3.34 |
  | yaml-cpp | 0.9.0 (Ubuntu 26.04 hat 0.8.0) |
  | ffmpeg | 9.1 |
  | LLVM-OpenMP | 23.1 |
- Auf ARM64 bietet MSYS2 nur clang (CLANGARM64). Auf x64 wird GCC verwendet (UCRT64).
- Lokal gibt es keinen Windows-Rechner. Gebaut und getestet wird unter Windows nur
  in GitHub Actions, und jede Iteration braucht einen Push. Den Feature-Branch
  darf Claude dafür so oft wie nötig nach origin pushen (vom Nutzer freigegeben).
  `main` wird nur nach Freigabe gemergt und gepusht.

## Ansätze

| | Ansatz | Abwägung |
|---|---|---|
| 1 | MSYS2 mit dem bestehenden Makefile, Windows-Code per `#ifdef _WIN32` | Wenig Umbau. Dieselben Bibliotheken und dasselbe Makefile wie unter macOS und Linux. Deckt x64 und ARM64 ab. Nutzer müssen MSYS2 installieren. |
| 2 | Visual Studio mit CMake und vcpkg | Der klassische Windows-Weg ohne MSYS2. Dafür ein zweites Build-System. vcpkg baut OpenBLAS und Armadillo aus dem Quelltext, das macht die CI-Läufe lang. Das Standard-OpenMP von MSVC (2.0) lehnt die `uword`-Schleifenindizes in `ClassdXdt.cpp` ab. |
| 3 | Cygwin | Der Code liefe fast unverändert. Das Programm braucht dann aber immer die Cygwin-Umgebung, und Cygwin gibt es nur für x64. |

Gewählt wurde **1** (vom Nutzer: „ja, Weg 1“).

## Verhalten unter Windows

### Integrator-Menü und Exit-Prompt

Die Bedienung ist dieselbe wie unter macOS und Linux.

- **Wann das Menü erscheint:** nur, wenn stdin eine echte Windows-Konsole ist.
  `_isatty` allein reicht dafür nicht, denn es meldet auch für `NUL` „ja“, also
  etwa für `</dev/null` in der MSYS2-bash. Maßgeblich ist, dass `GetConsoleMode`
  für den stdin-Handle gelingt. Sonst gilt wie heute still der Wert aus
  `config.yaml`, angezeigt durch die Zeile `Using: …`.
- **Tasten:** Eine Taste wird ohne Enter über `_getch()` gelesen.
  - `1` bis `5` wählen einen Integrator.
  - Enter (`\r`), Strg+D (4) und Strg+Z (26) übernehmen den Config-Wert.
    Strg+Z ist unter Windows das übliche Zeichen für Dateiende.
  - Pfeil- und Funktionstasten liefern bei `_getch` zwei Codes: erst 0 oder 0xE0,
    dann den Scan-Code. Beide werden gelesen, und es erscheint genau eine Meldung
    „Invalid key“. Unter POSIX prüft das `test_arrow_key_single_message`.
- **Eingabe leeren:** nach einer ungültigen Taste und vor dem Exit-Prompt, über
  `FlushConsoleInputBuffer`.
- **Strg+C:** Das Programm endet wie jedes Konsolenprogramm. Der
  Standard-Handler greift im Menü aber nicht: Während `_getch()` wartet, setzt
  die UCRT den Konsolenmodus auf 0, also ist auch `ENABLE_PROCESSED_INPUT` aus.
  Strg+C kommt dann als Taste 3 an, und die Konsole löst kein `CTRL_C_EVENT`
  aus. Bei Taste 3 beendet das Menü das Programm deshalb selbst mit
  `ExitProcess(STATUS_CONTROL_C_EXIT)`, wie es der Standard-Handler täte. Den
  Konsolenmodus hat `_getch()` dann schon wiederhergestellt, es bleibt nichts
  zurückzusetzen. `GenerateConsoleCtrlEvent` mit erneutem Lesen scheidet aus:
  Das Ereignis kommt asynchron und könnte das Programm erst im nächsten
  `_getch()` beenden, mit Konsolenmodus 0. Die MSYS2-bash sieht Status 130 wie
  unter POSIX, weil die MSYS2-Laufzeit `STATUS_CONTROL_C_EXIT` auf SIGINT
  abbildet. Das prüft `test_ctrl_c_ends_windows_menu`.
- **Hintergrundjobs:** Unter Windows gibt es keine; die Prüfung über `tcgetpgrp`
  entfällt.

### Video

- **Binärmodus:** `_popen(cmd, "wb")`. Im Textmodus würde Windows jedes Byte 0x0A
  zu 0x0D 0x0A erweitern und damit die Frames zerstören.
- **Dateiname:** steht in doppelten Anführungszeichen, weil `cmd.exe` keine
  einfachen kennt.
- **SIGPIPE:** gibt es unter Windows nicht. Stirbt ffmpeg, schlägt `fwrite` fehl,
  und es greift der bestehende Pfad „could not write all frames to ffmpeg“.
- **Exit-Status:** `_pclose` liefert direkt den Exit-Code, die
  `WIFEXITED`-Makros entfallen. Die Meldung lautet wie bisher „ffmpeg returned N.
  Is ffmpeg on the PATH?“. Fehlt ffmpeg, endet `cmd.exe` mit Code 9009 (nicht 1),
  was zur selben Meldung führt.

### Bauen und Starten

- In der MSYS2-Shell UCRT64 (x64) bzw. CLANGARM64 (ARM64) werden die Pakete mit
  `pacman -S` installiert, dann folgt `make Release`. Das Binary heißt
  `bin/Release/DiscontinousGalerkin.exe`.
- Starten lässt es sich in der MSYS2-Shell oder in Windows Terminal bzw.
  PowerShell. Außerhalb von MSYS2 muss dessen `bin`-Verzeichnis im PATH stehen,
  zum Beispiel `C:\msys64\ucrt64\bin`, weil dort die DLLs liegen.
- ffmpeg kann aus MSYS2 kommen oder aus einer anderen Quelle; es muss nur im PATH
  liegen.

## Umsetzung (Rahmen)

- **`src/main.cpp`:**
  - Die drei Terminal-Operationen werden kleine Funktionen: prüfen, ob stdin ein
    Terminal ist, eine Taste lesen und die Eingabe leeren.
  - Jede hat eine POSIX- und eine Windows-Implementierung, zusammen in einem
    `#ifdef _WIN32`-Block.
  - `selectTimeIntegration` und der Exit-Prompt rufen nur noch diese Funktionen
    auf. Tastenlogik und Meldungen bleiben gemeinsam.
  - Der POSIX-Code wird nur verschoben, nicht verändert. Das gilt auch für den
    SIGINT-Handler und die Hintergrundjob-Prüfung.
- **`src/VideoPipe.cpp`:** Je Plattform eigene Varianten für `popen`/`pclose`,
  das Quoting, SIGPIPE und die Auswertung des Status. Die ffmpeg-Pipe liegt dort
  und nicht in `ClassPlot.cpp`.
- **`Makefile`:** ein dritter Zweig für MSYS2, wenn `uname -s` mit `MINGW`,
  `UCRT64`, `CLANG64` oder `CLANGARM64` beginnt. In der CI liefert `uname -s` auf
  beiden Runnern `MINGW64_NT-<Version>` (ARM64 z. B. `MINGW64_NT-10.0-26200-ARM64`),
  und `uname -m` meldet auch auf ARM64 `x86_64`.
  - Compiler: g++ in den GCC-Umgebungen, clang++ in den clang-Umgebungen, sofern
    `CXX` nicht gesetzt ist. Weil `uname` die Umgebung nicht unterscheidet,
    entscheidet `MSYSTEM` (clang++ für `CLANG*`).
  - Flags und Bibliotheken: `-fopenmp`, `-lopenblas -lyaml-cpp`. Das OpenBLAS aus
    MSYS2 enthält LAPACK, `-llapack` ist nicht nötig.
- **Tests:** Windows wird wie im Makefile über `uname -s` erkannt.
  - `test-render` und `test-input-checks` laufen. Wo nötig, werden Ausgaben ohne
    `\r` verglichen, denn stdout im Textmodus schreibt CRLF.
  - `test-video`:
    - Der Frame-Zähl-Test läuft.
    - Der ffmpeg-Fehler-Test bekommt ein Fake-`ffmpeg.cmd` (`exit /b 1`) statt
      des Shell-Skripts. `cmd.exe` findet keine Shell-Skripte.
    - Der Speichertest wird übersprungen, weil `time(1)` unter MSYS2 keine
      nativen Windows-Prozesse misst.
  - `test-menu`: Die Tests laufen auch unter Windows, über den
    ConPTY-Testhelfer (siehe unten). `run_tty` benutzt unter Windows den Helfer
    statt `script`. Nur drei Tests prüfen reines POSIX-Verhalten und werden
    unter Windows mit Begründung übersprungen:
    - `test_ctrl_c_restores_terminal` (termios),
    - `test_ignored_sigint_stays_ignored` (Signal-Disposition),
    - `test_background_job_skips_menu` (Job-Control).

    Umgekehrt prüft `test_ctrl_c_ends_windows_menu` Strg+C nur unter Windows;
    unter POSIX entfällt er mit SKIP, dort prüft das
    `test_ctrl_c_restores_terminal`.
  - Jeder SKIP nennt seinen Grund. `make test` endet unter Windows mit exit 0,
    solange kein Test fehlschlägt.
- **ConPTY-Testhelfer** (`tests/conpty_run.cpp`, nur unter Windows gebaut, vom
  Nutzer gewählt): das Gegenstück zu `script` für `run_tty`.
  - Er startet einen Befehl in einer Pseudokonsole: `CreatePseudoConsole`, dann
    `CreateProcess` mit `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`. Damit sieht das
    Programm eine echte Konsole, und `GetConsoleMode` sowie `_getch` arbeiten.
  - Er reicht seine stdin an die Pseudokonsole weiter, wie `script`. So
    funktionieren die Tastenfolgen und Verzögerungen von `run_tty` unverändert.
  - Er gibt die Ausgabe der Konsole auf stdout aus und endet mit dem Exit-Code
    des Programms.
  - Endet seine stdin, schickt er ein Dateiende, so wie `script` beim Schließen
    der Eingabe ein ^D sendet. Unter Windows ist das Strg+Z, gefolgt von Enter.
  - Die Pseudokonsole schreibt eigene Steuersequenzen (VT) in die Ausgabe. Die
    Tests vergleichen Textstücke; reicht das nicht, filtert der Helfer die
    Sequenzen heraus.
- **CI (`.github/workflows/ci.yml`):** ein zweiter Job `windows`.
  - Seine Matrix enthält zwei Einträge:
    - `windows-2025` mit UCRT64 und den Paketen `mingw-w64-ucrt-x86_64-*`,
    - `windows-11-arm` mit CLANGARM64 und den Paketen `mingw-w64-clang-aarch64-*`.
  - `msys2/setup-msys2` installiert make, die Toolchain (`gcc` bzw. `clang` und
    `llvm-openmp`; ein Paket `openmp` gibt es in CLANGARM64 nicht) und die
    Bibliotheken Armadillo, OpenBLAS, yaml-cpp und ffmpeg.
  - Ein weiterer Schritt startet `bin\Release\DiscontinousGalerkin.exe` aus
    PowerShell, also außerhalb von MSYS2, mit dem `bin`-Verzeichnis der Umgebung
    im PATH. Das Programm findet DLLs und ffmpeg und schreibt `output.mp4`.
  - Die Schritte `make Release Debug` und `make test` laufen in `shell: msys2 {0}`.
- **README:** ein neuer Abschnitt „Windows (MSYS2)“ mit Paketliste, Bauen und
  Starten sowie Hinweisen zu PATH und DLLs, ffmpeg und dem Menü.

## Vom Builder entschieden (Claudes Calls, korrigierbar)

- „Soll einfach auch unter Windows laufen“ bedeutet: aus dem Quelltext bauen und
  starten wie unter macOS und Linux. Es gibt keinen Installer und keine gebündelte
  `.exe`.
- Auf x64 wird UCRT64 verwendet, die Standardumgebung von MSYS2, nicht MINGW64.
- Strg+Z zählt wie Strg+D als „Config-Wert“.
- Das Menü erscheint nur in einer echten Konsole (`GetConsoleMode`), sonst gilt
  der Config-Wert.
- Der Speichertest wird unter Windows übersprungen. macOS und Linux decken ihn ab.
- Strg+Z steht ohne `#ifdef` im gemeinsamen Code. Unter POSIX bleibt ISIG im
  Raw-Modus an, Strg+Z hält dort den Job an, daher entfällt
  `test_ctrl_z_uses_config` unter POSIX mit SKIP.
- Die Windows-Jobs laufen wie die Linux-Jobs mit `fail-fast: false`.
- Die drei POSIX-spezifischen Menü-Tests werden unter Windows übersprungen und
  nicht nachgebaut. Zusammen mit `test_memory_bounded` aus den Video-Tests
  (siehe oben) sind es vier SKIPs in `make test`. Strg+C prüft unter Windows ein
  eigener Test, `test_ctrl_c_ends_windows_menu` (siehe „Strg+C“ oben).
- `make` kommt aus der MSYS-Umgebung (Paket `make`), nicht `mingw32-make`.

## Befunde aus der CI

Das war bei der Umsetzung offen und ist in der CI geklärt (beide Windows-Jobs grün):

- OpenBLAS aus MSYS2 enthält LAPACK. `-llapack` ist nicht nötig.
- yaml-cpp 0.9 baut mit C++14 unverändert.
- `msys2/setup-msys2` unterstützt `windows-11-arm`.
- `uname -s` liefert auf beiden Runnern `MINGW64_NT-<Version>`, `uname -m` auch auf
  ARM64 `x86_64`. Die Compiler-Wahl läuft deshalb über `MSYSTEM`.
- Die ffmpeg-Pipe steht in `src/VideoPipe.cpp`. Im Textmodus (`"w"`) wurden
  0x0A-Bytes zu 0x0D 0x0A, im Test kamen 1028 statt 1024 Bytes an; `"wb"` behebt
  das. `cmd.exe` liefert für einen unbekannten Befehl 9009.
- CRLF in der Ausgabe stört den Frame-Zähl-Vergleich im Video-Test unter Windows
  nicht. `tr -d '\r'` bleibt als Absicherung.
- `_getch` hält den zweiten Code einer Funktions- oder Pfeiltaste innerhalb der
  CRT, außerhalb der Reichweite von `FlushConsoleInputBuffer`. Deshalb liest das
  Menü beide Codes (`kExtendedKey`).
- Die Menü-Tests laufen unter Windows über den ConPTY-Helfer. Windows-SKIPs:
  `test_memory_bounded`, `test_ctrl_c_restores_terminal`,
  `test_ignored_sigint_stays_ignored`, `test_background_job_skips_menu`.
- Die Terminal-Probe von `run-integrator-menu.sh` (`test -t 0`) stimmt auch mit
  dem ConPTY-Helfer: Die MSYS2-bash sieht die Pseudokonsole als Terminal.
  Schlüge die Probe fehl, prüft das Skript unter Windows ersatzweise, ob das
  Menü des Solvers erscheint.
- Strg+C im Menü kam als Taste 3 an, das Menü meldete „Invalid key“ und lief
  weiter (`test_ctrl_c_ends_windows_menu` war rot). Seit
  `ExitProcess(STATUS_CONTROL_C_EXIT)` endet das Programm, und die MSYS2-bash
  sieht Status 130.
- argv[0] enthält unter Windows Backslashes, beim Start aus PowerShell wie aus
  der MSYS2-bash. Die Suche nach `config.yaml` neben der Binary trennte nur an
  `/` und fand die Datei nicht; `test_config_found_next_to_binary` und der
  PowerShell-Schritt ohne Argument waren rot. Jetzt trennt sie an `/` und `\`.

## Offen

- **Gegenprobe Exit-Prompt:** Ohne `FlushConsoleInputBuffer` blieb
  `test_enter_after_key_does_not_skip_exit_prompt` unter Windows grün. Ein
  vorab getipptes Enter überlebt dort bis zum Exit-Prompt auch ohne den Aufruf
  nicht. Die Ursache ist unbekannt; Verdächtige sind `cmd.exe` und ffmpeg, die
  `_popen` während des Zeichnens startet, oder der Konsolen-Host. Der Aufruf
  bleibt als Absicherung, aber nur unter POSIX ist die Gegenprobe wirksam.
- Erscheint das Menü auch im MSYS2-Terminal (mintty)? Die CI kann das nicht
  prüfen; das README sagt es so.
- Ein echtes Zeichen 0xE0 in der Codepage der Konsoleneingabe (CP850 „Ó“,
  CP437 „α“) hält das Menü für das Präfix einer Sondertaste und verschluckt die
  nächste Taste. Das ist selten und wird hingenommen.

## Prüfung

- **macOS:** `make test` ist grün (54 PASS, nur die SKIPs von
  `test_ctrl_z_uses_config` und `test_ctrl_c_ends_windows_menu`).
- **Linux:** Die CI (x86_64, arm64) und die Docker-Images (Ubuntu 24.04/26.04,
  arm64/x64) sind grün wie bisher.
- **Windows (CI, beide Runner):**
  - `make Release Debug` läuft ohne Fehler.
  - `make test` endet mit exit 0, und es gibt nur die oben genannten SKIPs.
  - Das Video hat die erwartete Frame-Zahl (ffprobe).
- **Windows-Menü-Tests:** Die Menü-Tests laufen über den ConPTY-Helfer, nur
  die drei POSIX-spezifischen sind übersprungen („3 skipped“ in der Menü-Suite).
  Der vierte SKIP von `make test`, `test_memory_bounded`, gehört zu den
  Video-Tests.
- **Gegenproben**, je einmal in der CI (die dritte war unter Windows nicht rot,
  siehe „Offen“):
  - Mit `"w"` statt `"wb"` muss der Frame-Test unter Windows fehlschlagen.
  - Ohne das Lesen des zweiten Codes einer Pfeiltaste muss
    `test_arrow_key_single_message` fehlschlagen.
  - Ohne `FlushConsoleInputBuffer` am Exit-Prompt muss
    `test_enter_after_key_does_not_skip_exit_prompt` fehlschlagen.
  - Ohne die Behandlung von Taste 3 muss `test_ctrl_c_ends_windows_menu`
    fehlschlagen.
  - Mit `find_last_of('/')` müssen unter Windows
    `test_config_found_next_to_binary` und der PowerShell-Schritt ohne Argument
    fehlschlagen.
