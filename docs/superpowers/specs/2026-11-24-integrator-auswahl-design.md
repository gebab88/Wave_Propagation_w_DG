# Integrator-Auswahl beim Programmstart — Design

## Ziel

Beim Start zeigt das Programm ein nummeriertes Menü der Zeitintegrationsverfahren.
Der Nutzer wählt per **einzelnem Tastendruck** (ohne Enter). So lässt sich das
Verfahren wechseln, ohne `config.yaml` zu bearbeiten.

```
Select time integration method:
  1) EulerExplicit           Euler, 1st order, needs small CFL
  2) RungeKuttaClassic       classic RK4, 4th order
  3) RungeKuttaSecond        Kutta 3/8 rule, 4th order
  4) RungeKuttaSecondOrder   Heun, 2nd order, cheapest at default CFL
  5) PredictorCorrectorHeun  RK4 + iterated trapezoid, slowest   (config.yaml)
Press 1-5, Enter = config.yaml value:
```

**Beschreibungsspalte** (Nachtrag 2026-11-24): Jede Zeile trägt eine Kurzbeschreibung,
beginnend eine feste Spalte nach dem längsten Namen plus zwei Leerzeichen; Zeilen
≤ 80 Zeichen. Grundlage sind Messungen mit der Default-Config (N=50, Ordnung 5):
Euler divergiert bei `CFLReserve` 0,45 und 0,2 und ist erst ab etwa 0,1 stabil
(dann mit Fehlern 1. Ordnung); `RungeKuttaSecond` ist Kuttas 3/8-Regel (4. Ordnung);
`PredictorCorrectorHeun` braucht etwa 13× so lange wie RK4. Keine Zahlen im Menütext,
weil die Stabilitätsgrenze von N, Ordnung und Laufzeit abhängt.

## Nicht-Ziele

- `config.yaml` wird nicht abgelöst: `TimeIntegration` bleibt Pflichtfeld.
- Keine Abfrage weiterer Parameter (N, Ordnung, …).
- Keine Änderung an den Integratoren selbst.

## Anti-Ziele

- Benchmark-Skript, Docker-Smoke-Test und `</dev/null`-Workflow dürfen nicht
  blockieren oder ihr Verhalten ändern.
- Nach Ctrl-C während der Abfrage darf das Terminal nicht verstellt bleiben.

## Ansätze

| | Ansatz | Abwägung |
|---|---|---|
| A | Zahl + Enter via `std::cin` | ~15 Zeilen, maximal robust; eine Taste mehr |
| **B** | **Einzeltastendruck via `termios`-Rohmodus** | sofortige Auswahl; ~40 Zeilen, Terminalzustand muss sicher zurückgesetzt werden |

**Gewählt: B** (Wunsch des Nutzers: „per Tastendruck“).

## Verhalten

- **Zeitpunkt:** direkt nach dem Lesen von `TimeIntegration` aus der Config,
  vor dem Matrix-Setup.
- **Vorauswahl:** der Config-Wert ist markiert; **Enter** übernimmt ihn.
- **Tasten `1`–`5`:** wählen sofort das Verfahren; die Wahl wird bestätigt
  ausgegeben (`Using: RungeKuttaClassic`).
- **Ungültige Taste:** Hinweis ausgeben, erneut lesen.
- **Kein Terminal** (`isatty(STDIN_FILENO)` falsch, z. B. `</dev/null`, Pipe,
  Docker ohne TTY): keine Abfrage, Config-Wert gilt unverändert.
- **EOF beim Lesen:** Config-Wert gilt.
- **Ctrl-C:** `ISIG` bleibt aktiv; ein für die Dauer der Abfrage installierter
  `SIGINT`-Handler stellt den alten `termios`-Zustand wieder her und beendet das
  Programm dann regulär per Signal (Default-Handler + `raise`).

## Umsetzung (Rahmen)

- Eine statische Funktion in `src/main.cpp`, z. B.
  `std::string selectTimeIntegration(const std::string &configDefault)`;
  die Liste der Verfahrensnamen als ein Array, das Menü und Validierung teilen.
- `termios`: `tcgetattr` sichern, `ICANON` und `ECHO` löschen, `VMIN=1`,
  `VTIME=0`, Byte per `read` lesen, danach immer zurücksetzen.
- Kein neues Quellfile → keine Makefile-Änderung.

## Vom Builder entschieden (Claudes Calls, korrigierbar)

- **Ungültiger Config-Wert wird sofort beim Start abgewiesen** (vorher erst nach
  dem Setup). Das Menü braucht ohnehin eine gültige Vorauswahl.
- **Menütext auf Englisch**, passend zur übrigen Programmausgabe.
- README-Abschnitt „Ausführen“ bekommt zwei Sätze zur Abfrage.

## Prüfung

- Build ohne neue Warnungen.
- Nicht-interaktiv: `tests/linux-smoke-config.yaml` mit `</dev/null` läuft
  ohne Abfrage durch.
- Interaktiv (per `script` simuliertes TTY): Taste `2` → RK4, Enter →
  Config-Wert, ungültige Taste → erneute Abfrage.
- Ctrl-C während der Abfrage: Terminal danach wieder normal (`stty -a` zeigt
  `icanon echo`).
