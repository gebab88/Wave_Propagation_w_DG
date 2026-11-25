# Speicherdeckel für den Frame-Puffer des Videos

Kurzentwurf, Folgepunkt zu Punkt 2 der Verbesserungsliste. Stand 2026-11-25.

## Absicht

Seit Punkt 2 legt das Programm mit Plot vor der Rechnung einen Puffer für alle
Video-Frames an: Frames × N·order Druckwerte × 8 Byte. Das Limit von
1 000 000 Frames begrenzt nur die Videolänge, nicht diesen Speicher. Bei
N = 2048, order 5 und `VideoTimePerSecond: 1e-5` wären es 9,8 GB, am
Frame-Limit sogar 82 GB. Passt das nicht in den RAM, bricht das Programm mit
`std::bad_alloc` ab oder wird unter Linux vom OOM-Killer beendet; macOS lagert
vorher aus und wird sehr langsam. Der Entwurf zu Punkt 2 wollte genau diesen
Absturz beim Anlegen des Puffers durch eine klare Meldung ersetzen.

## Ziel

- Mit Plot prüft das Programm vor dem Menü, wie viel Speicher der Frame-Puffer
  braucht, und bricht mit `Config error` und Status 1 ab, wenn es mehr als
  **60 % des eingebauten RAMs** wären.
- Der Bedarf wird vor dem Anlegen aus Frame-Zahl, N und order berechnet,
  ohne Überlauf (Rechnung in `double`).
- Die Meldung nennt den Bedarf, die Grenze und was man ändern kann, etwa:

  ```
  Config error: the video needs 9.8 GB (120001 frames x 10240 values); at most
  4.8 GB (60% of the RAM) are allowed. Increase VideoTimePerSecond, shorten
  t1 - t0 or reduce N or order.
  ```

- Der eingebaute RAM wird beim Start abgefragt: unter macOS und Linux mit
  `sysconf`, unter Windows mit `GlobalMemoryStatusEx`. Lässt er sich nicht
  ermitteln, gilt eine feste Grenze von 4 GB, und die Meldung sagt das.

## Nicht-Ziele

- Kein Blick auf den gerade freien Speicher, nur auf den eingebauten RAM.
- Keine Rücksicht auf Speichergrenzen von Containern (cgroups/Docker); dort
  gilt der RAM des Rechners.
- Die übrigen Speicherverbraucher (Matrizen der Rechnung, Render-Puffer von
  ein paar MB) zählen nicht mit.
- Weniger Werte pro Frame zu speichern (Ansatz C unten) gehört nicht dazu.

## Anti-Ziele

- Ohne Plot wird nichts geprüft, weil nichts gespeichert wird.
- Läufe, die heute in den Speicher passen, laufen weiter; die Standard-Config
  braucht 2,4 MB.
- Das Limit von 1 000 000 Frames bleibt, es begrenzt die Videolänge.
- Die lokale `config.yaml` des Nutzers wird nicht angefasst.

## Rahmen

- C++14, bestehendes Makefile, keine neuen Bibliotheken; macOS, Linux
  (auch Raspberry Pi) und Windows (MSYS2).
- Das `#ifdef _WIN32` für die RAM-Abfrage steht nur in einer eigenen kleinen
  Funktion, wie bei den Terminal-Funktionen in `src/main.cpp`.
- Alle Tests laufen über `make test`; Doku Deutsch, Code-Kommentare und
  Commits Englisch.

## Ansätze

- **A – fester Wert (z. B. 4 GB):** einfach und überall gleich, aber auf
  einem Pi mit 4 GB schützt er nicht und auf einem Mac mit 64 GB ist er
  unnötig streng.
- **B – Anteil des eingebauten RAMs** (gewählt): passt sich an Pi und Mac an;
  dafür eine kleine plattformabhängige Funktion, und dieselbe Config kann
  auf dem Mac laufen und auf dem Pi abgelehnt werden. Das ist gewollt.
- **C – weniger Werte pro Frame** (Minimum und Maximum pro Pixelspalte):
  Speicher pro Frame unabhängig von N, aber größerer Umbau, und Punkt 9
  (Datenexport) braucht die volle Auflösung.
- `std::bad_alloc` abzufangen reicht nicht: Linux beendet oft erst später
  über den OOM-Killer, macOS lagert vorher aus.

## Entschieden

- Vom Nutzer: Ansatz B, Grenze **60 %** des eingebauten RAMs.
- Von mir, zur Prüfung:
  - Fallback 4 GB, wenn der RAM nicht ermittelt werden kann.
  - Gezählt werden die Frame-Werte und die Frame-Zeiten
    (Frames × (N·order + 1) × 8 Byte).
  - Angabe in GB (10⁹ Byte) mit einer Nachkommastelle.
  - Reihenfolge: erst das Frame-Limit, dann der Speicherdeckel.
- Nach der Code-Review, vom Nutzer beauftragt (2026-11-25):
  - Der Bedarf wird auf- und die Grenze abgerundet, damit ein Bedarf knapp
    über der Grenze nicht als dieselbe Zahl erscheint (vorher etwa „needs
    41.2 GB … at most 41.2 GB“).
  - Das Gitter (N + 1 Punkte) wird erst nach allen Config-Prüfungen angelegt.
    Vorher belegte ein riesiges N schon vor der Prüfung Speicher, bei
    N = 2·10⁸ etwa 1,6 GB.
  - Scheitert das Anlegen des Puffers trotz bestandener Prüfung (andere
    Prozesse, strenges Overcommit, Adressraum), endet das Programm mit
    „Error: the video buffer of … GB could not be allocated …“ und Exit-Code 1
    statt mit einem nicht abgefangenen `std::bad_alloc`. Das ersetzt die
    Prüfung nicht, siehe oben, es ist nur der Rückfall.
  - 32-Bit-Systeme bleiben außen vor: Der Pi läuft mit 64 Bit.

## Tests

- Unit-Test: Bedarf in Byte für bekannte Werte; die Grenze ist 60 % des
  übergebenen RAMs bzw. 4 GB ohne RAM-Angabe; die RAM-Abfrage liefert mehr
  als 250 MB (ein Pi mit „1 GB“ meldet nur gut 0,9 GB); die GB-Angaben sind
  richtig gerundet.
- `run-input-checks.sh`:
  - Eine Config mit Plot, die weit über jeden RAM geht (etwa N = 100 000,
    order 8, `VideoTimePerSecond: 1e-8` bei t1 = 8e-5: 480 001 Frames, rund
    3 TB), wird sofort mit Meldung abgelehnt, ohne etwas anzulegen.
  - Eine Config, die mit Plot deutlich über 60 % jedes üblichen RAMs läge,
    ohne Plot aber in Sekunden durchrechnet, läuft ohne Plot durch
    (N = 20 000, order 8, t1 = 8e-8, `VideoTimePerSecond: 5.333e-12`:
    900 057 Frames, rund 1,15 TB mit Plot, etwa 32 Schritte).
- `run-video-render.sh`:
  - N = 2·10⁸ mit Plot wird abgelehnt, ohne mehr als 128 MiB zu belegen.
  - Nur unter Linux: Mit `ulimit -v` von 1 GB scheitert ein Puffer von 2 GB,
    und das Programm meldet das mit Exit-Code 1.
- Die bestehenden Video- und Speichertests laufen unverändert grün.

## Doku

`config_example.yaml` (Kommentar zu `VideoTimePerSecond`), README (Absatz zur
Prüfung der Config).

## Dem Builder überlassen

Wo die RAM-Abfrage und die Bedarfsrechnung liegen (eigene kleine Datei oder
bei `FrameSampler`), und wie der Unit-Test die Grenze ohne echten RAM-Wert
prüft.
