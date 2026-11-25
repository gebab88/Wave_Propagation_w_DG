# Video mit fester Wiedergabegeschwindigkeit statt eines Frames pro Zeitschritt

Kurzentwurf, Punkt 2 der Verbesserungsliste. Stand 2026-11-25.

## Absicht

Das Video dient dazu, die Wellenausbreitung anzuschauen. Heute zeichnet das
Programm pro Zeitschritt einen Frame und hält dafür den Druck **jedes**
Zeitschritts im Speicher (`src/main.cpp`, Zeitschleife mit `storeSnapshot`).
Daraus folgen zwei Probleme:

- **Tempo und Länge hängen vom Gitter ab.** Der Zeitschritt schrumpft mit
  `dx`, also gibt es bei feinerem Gitter mehr Frames. Mit der Standard-Config
  (N = 50, order = 5, t1 = 0,02 s) sind es etwa 12 000 Frames, also 200 s
  Video; eine Video-Sekunde zeigt 0,1 ms. Mit N = 2048 wären es etwa
  490 000 Frames (2¼ Stunden).
- **Der Speicher wächst etwa mit N².** Werte pro Schritt und Schrittzahl
  wachsen beide mit N. Bei N = 2048 wären es rund 40 GB, kurzzeitig doppelt
  so viel, weil `trans()` die Matrix am Ende kopiert. Auch mit
  `EnablePlotting: false` wird alles gespeichert, obwohl nichts die Daten
  verwendet.

## Ziel

- Neuer optionaler Config-Schlüssel `VideoTimePerSecond`: Simulationszeit in
  Sekunden pro Sekunde Video. Standard, wenn der Schlüssel fehlt: `1.0e-3`.
  Die Welle läuft damit in jedem Lauf gleich schnell über den Bildschirm,
  unabhängig von Gitter, Ordnung und Zeitintegrator.
- Bei 60 fps liegen die Frames im Abstand `Δ = VideoTimePerSecond / 60` bei
  `t_k = t0 + k·Δ`, k = 0 … K, mit `K = floor((t1 − t0) / Δ)` (mit kleiner
  Toleranz, damit ein exaktes Vielfaches von Δ bei t1 noch zählt). Frame 0 ist
  der Anfangszustand.
- Frame k entsteht in der Zeitschleife durch lineare Interpolation zwischen
  den beiden Rechenschritten, die `t_k` einschließen. Die Frame-Zeiten stimmen
  so exakt, auch wenn ein Rechenschritt länger ist als Δ (grobe Gitter: dann
  fallen mehrere Frames in einen Schritt).
- Mit `EnablePlotting: false` speichert das Programm gar keine Zwischenstände.
- Speicher mit Plot: (K + 1) × Druckwerte pro Frame. Bei den Standardwerten
  1201 Frames, bei N = 2048 etwa 100 MB.

Mit der Standard-Config entsteht so ein Video von 20 s mit 1201 Frames; ein
Durchlauf der Welle durch das Gebiet (0,5 m bei 300 m/s) dauert 1,7 s, pro
Frame wandert sie 5 mm (1/20 Wellenlänge).

## Nicht-Ziele

- Bildrate (60 fps), Auflösung und die feste y-Achse bleiben.
- Kein Streamen der Frames an ffmpeg während der Rechnung.
- Kein Datenexport (Punkt 9). Die Auswahl der Frame-Zeitpunkte soll aber als
  eigenes kleines Stück Code entstehen, das Punkt 9 später wiederverwenden
  kann.

## Anti-Ziele

- Die berechnete Lösung ändert sich nicht, nur was davon gespeichert wird.
- Die Rechnung wird nicht messbar langsamer.
- Bestehende Configs laufen ohne Änderung weiter; die lokale `config.yaml` des
  Nutzers wird nicht angefasst.

## Rahmen

- C++14, bestehende Bibliotheken und Makefile; macOS, Linux und Windows
  (MSYS2) wie bisher, ohne neues `#ifdef`.
- Die getrennte Zeitmessung für Rechnen und Plotten bleibt. Bei einem
  Stabilitätsabbruch entsteht wie bisher kein Video.
- Alle Tests laufen über `make test`; Doku auf Deutsch, Code-Kommentare und
  Commits auf Englisch.

## Ansätze

1. **Frames während der Rechnung abgreifen, danach rendern** (gewählt). Ein
   Puffer genau der Frame-Zahl wird vorab angelegt; die Zeitschleife behält den
   vorigen Druck und interpoliert, sobald ein Schritt einen Frame-Zeitpunkt
   überschreitet. `ClassPlot` bleibt unverändert und bekommt nur weniger
   Zeilen.
2. **Frames direkt an ffmpeg streamen.** Fast kein Speicher, aber Rendern und
   Kodieren laufen in der Zeitschleife: Die Rechenzeit enthält das Plotten, ein
   Stabilitätsabbruch hinterlässt ein halbes Video, `ClassPlot` müsste
   umgebaut werden. Für ~100 MB lohnt das nicht.
3. **Jeden k-ten Schritt speichern.** Einfach, aber die Frame-Abstände stimmen
   nur ungefähr, das Tempo schwankt, und bei groben Gittern entstehen doppelte
   Bilder.

Der Nutzer hat Variante B (feste Wiedergabegeschwindigkeit statt fester
Videolänge) und Ansatz 1 gewählt.

## Entschieden

- Vom Nutzer: feste Wiedergabegeschwindigkeit (Simulationszeit pro
  Video-Sekunde), Ansatz 1.
- Von mir, zur Prüfung:
  - Schlüsselname `VideoTimePerSecond`, Standard `1.0e-3` (1 ms pro
    Video-Sekunde).
  - Lineare Interpolation zwischen den Rechenschritten statt Einrasten auf den
    nächsten Schritt.
  - Prüfung wie bei den anderen Config-Werten, vor dem Menü: endlich und > 0,
    auch wenn das Plotten aus ist. Mit Plot zusätzlich ein Fehler, wenn mehr
    als 1 000 000 Frames entstünden (gut 4½ Stunden Video), statt eines
    Absturzes beim Anlegen des Puffers.

## Tests

- Unit-Test für die Frame-Auswahl: Anzahl und Zeitpunkte der Frames,
  Interpolation, exakte Treffer, mehrere Frames in einem Rechenschritt,
  Ende genau bei t1. Neue Suite in `make test`.
- `run-video-render.sh`: Zwei Läufe mit verschiedenem N bei gleichem t1 und
  `VideoTimePerSecond` melden dieselbe, erwartete Frame-Zahl.
- Speicher ohne Plot: Ein Lauf mit vielen Schritten und `EnablePlotting: false`
  bleibt weit unter dem, was das Speichern aller Schritte bräuchte (Messung mit
  `time(1)` wie beim bestehenden Speichertest; unter Windows SKIP).
- `run-input-checks.sh`: 0, negativ, `.nan`, `.inf` und zu viele Frames werden
  mit Meldung abgelehnt.
- Der bestehende `test_memory_bounded` soll weiter viele Frames rendern; dafür
  setzt seine Config `VideoTimePerSecond` passend klein (etwa 1.0e-4, also
  wieder ~12 000 Frames).

## Doku

`config_example.yaml` (neuer Schlüssel mit Kommentar), README („Konfiguration“,
„Ausführen“: nicht mehr ein Frame pro Zeitschritt, „Performance“: Frame-Zahl).

## Dem Builder überlassen

Wo die Frame-Auswahl lebt (eigene kleine Klasse oder Funktion mit eigenem
Header) und in welchem Layout der Puffer angelegt wird, damit keine zweite
Kopie für `ClassPlot` nötig ist.
