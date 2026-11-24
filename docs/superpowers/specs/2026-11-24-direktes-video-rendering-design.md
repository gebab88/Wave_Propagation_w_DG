# Video direkt rendern statt gnuplot-PNGs — Design

## Ziel

Plotten + Video kostet mit der Default-Config ~12,7 s (gnuplot 8,6 s für 12002 PNGs,
ffmpeg 4,1 s), die Rechnung nur 0,7 s. Künftig zeichnet das Programm die Frames selbst
in einen RGB-Puffer und schreibt sie roh über eine Pipe in ffmpeg. Es entstehen keine
PNG-Dateien mehr, nur `output.mp4`.

Messung (Default-Config, alle 12002 Frames 800×600, 60 fps): x264 allein 6,4 s
(Default-Preset) bzw. 4,1 s (`veryfast`); Pipe-Overhead vernachlässigbar.
Erwartet: Plotten + Video ~4–4,5 s.

## Nicht-Ziele

- Keine Änderung an Rechnung, Integratoren oder Menü.
- Keine Einzelbilder mehr (vom Nutzer so entschieden: nur das Video zählt).

## Anti-Ziele

- Das Video darf inhaltlich nicht schlechter lesbar werden als heute (Achsen,
  Gitter, Zahlen bleiben).
- Fehlt ffmpeg, darf das Programm nicht hängen oder abstürzen: Meldung, weiter.

## Ansätze

| | Ansatz | Abwägung |
|---|---|---|
| A | Nur jedes 2. Frame mit gnuplot plotten | ~6–7 s, minimaler Code, PNGs bleiben |
| B | A + `-preset veryfast` | etwas schneller als A |
| **C** | **Frames im Programm zeichnen, roh an ffmpeg pipen** | ~4 s (alle Frames), keine PNGs, Zeichnen selbst implementiert |

**Gewählt: C** (Nutzer).

## Verhalten

- **Bildinhalt** wie der heutige gnuplot-Frame: 800×600, weißer Hintergrund,
  Rahmen, grau gestricheltes Gitter an den Ticks, Zahlen an x- und y-Achse,
  violette Kurve (Linie + `+`-Marker an den Knoten).
- **Achsen:** x von `x0` bis `x1` aus der Config (heute fest `[0:0.5]`, was der
  Default-Config entspricht), y fest `[-2:2]` wie heute. Werte außerhalb werden
  abgeschnitten.
- **Bildfolge:** **jeder** Zeitschritt wird ein Video-Frame (Nutzerwunsch); heute
  verwirft `-framerate 60 … -r 30` jedes zweite Bild. Ausgabe mit **60 fps**, damit
  Länge und Tempo des Videos gleich bleiben (~200 s).
- **ffmpeg fehlt / bricht ab:** Fehlermeldung wie heute, Programm läuft weiter bis
  zum Exit-Prompt.

## Umsetzung (Rahmen)

- `ClassPlot` behält Konstruktor + Aufruf aus `main.cpp`; `Plot()` und
  `MakeVideo()` werden zu einem Schritt, der rendert und kodiert.
- Hintergrund (Rahmen, Gitter, Beschriftung) wird einmal gezeichnet; pro Frame:
  Hintergrund kopieren, Kurve darüber zeichnen.
- Zahlen über eine kleine eingebettete Bitmap-Schrift (nur `0-9 . -`), keine neue
  Abhängigkeit.
- Ein `popen("ffmpeg -f rawvideo -pix_fmt rgb24 -s 800x600 -framerate 60 -i - …", "w")`;
  Frames werden in Blöcken parallel (std::thread) gerendert und der Reihe nach
  geschrieben. `SIGPIPE` wird während des Schreibens ignoriert, Schreibfehler
  beenden das Rendern sauber.

## Vom Builder entschieden (Claudes Calls, korrigierbar)

- **`-preset veryfast`**: spart ~2,3 s, Datei ~3 % größer, Qualität (CRF) gleich.
- **60 fps statt 30 fps** Ausgabe, damit das Video trotz doppelter Frame-Zahl gleich lang bleibt.
- **Legende `'-'` entfällt** (heute nur gnuplots Platzhalter ohne Aussage).
- **Kurve mit Antialiasing** (Wu-Linien), damit sie nicht treppig aussieht.
- **gnuplot fällt als Abhängigkeit weg**: README, `Dockerfile.linux-arm64`
  (`gnuplot-nox`). Make-Target `video` entfällt; `make clean` löscht `*.png`
  weiter (Altbestände).
- **Linux-Smoke-Test** prüft statt der PNG-Anzahl, dass `output.mp4` existiert
  und (per `ffprobe`) die erwartete Frame-Zahl hat.
- `docs/standing-wave.png` (README-Bild) bleibt unverändert.

## Prüfung

- Build ohne Warnungen; Menü-Tests weiter grün.
- Frame-Zahl des Videos = Zahl der gespeicherten Zeitschritte (per `ffprobe`).
- Sichtvergleich: ein Frame aus dem neuen Video neben dem gnuplot-Frame gleichen
  Zeitpunkts (Kurve deckungsgleich, Achsen gleich).
- Laufzeit Plotten mit Default-Config messen und im README eintragen.
- Ohne ffmpeg im `PATH`: Meldung, kein Hänger.
