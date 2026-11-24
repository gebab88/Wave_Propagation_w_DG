# Zeitintegration

Die DG-Raumdiskretisierung ([`discontinuous-galerkin.md`](discontinuous-galerkin.md)) macht aus der Wellengleichung ein System gewöhnlicher Differentialgleichungen für den Zustandsvektor $X(t) \in \mathbb{R}^m$, $m = 2nN$ (Linienmethode). Dieses Dokument beschreibt die fünf Verfahren, mit denen der Löser dieses System in der Zeit integriert, und wie sie sich in Stabilität, Genauigkeit und Laufzeit unterscheiden.

Zahlenwerte gelten für die Standardkonfiguration aus [`config_example.yaml`](../config_example.yaml) ($n$ = `order` = 5, $N = 50$, $m = 500$, `CFLReserve = 0.45`, $t_1 = 20$ ms), sofern nicht anders angegeben.

---

## 1 Das semidiskrete System

```math
\frac{\mathrm{d}X}{\mathrm{d}t} = f(t, X) = L\,X + g(t),
```

- $L \in \mathbb{R}^{m\times m}$ ist der lineare DG-Operator (Volumenterm, Flüsse, Wandreflexion). Er wird nie als Matrix aufgebaut, sondern bei jeder Auswertung angewendet.
- $g(t)$ ist der Quellterm aus dem einlaufenden Sinus am linken Rand, proportional zu $\sin(\omega t)$.

$f$ ist `ClassdXdt::FluidMatrix(X, dXdt, t)`. Eine Auswertung ist der teure Teil jedes Verfahrens; die Kosten eines Verfahrens zählen sich deshalb in Auswertungen pro Schritt.

### 1.1 Einbindung im Code

Jedes Verfahren ist eine Methode `void Verfahren(vec &X, double &t)` von `ClassdXdt`, die $X$ und $t$ um genau einen Schritt $\Delta t$ weitersetzt. `main()` wählt sie einmal über einen Zeiger auf die Memberfunktion aus und ruft sie in der Zeitschleife auf:

```cpp
using StepFn = void (ClassdXdt::*)(vec &, double &);
...
storeSnapshot();                 // Zustand bei t0
do {
    (dXdtObj.*step)(X, t);
    storeSnapshot();
} while (t < t1);
```

Daraus folgt:

- Die Zeit wird **innerhalb** des Verfahrens bis zu den Stufenzeitpunkten weitergesetzt. Jede Stufe wertet den Randquellterm $\sin(\omega t)$ damit zum richtigen Zeitpunkt aus.
- $t$ wird schrittweise aufaddiert ($t \mathrel{+}= \Delta t$), nicht als $t_0 + k\,\Delta t$ berechnet. Rundungsfehler sammeln sich dabei an. Der letzte Schritt kann $t_1$ um weniger als $\Delta t$ überschreiten.
- Die Stufenvektoren `k1` … `k4` sind Member von `ClassdXdt` und werden wiederverwendet (`k5`, `k6` sind angelegt, aber ungenutzt).

Auswahl: Schlüssel `TimeIntegration` in `config.yaml` oder per Tastendruck im Startmenü.

---

## 2 Übersicht

| `TimeIntegration` | Verfahren | Ordnung | $f$-Auswertungen pro Schritt | Stabilitätsgrenze `CFLReserve` | Rechenzeit (Standard) |
|---|---|---|---|---|---|
| `EulerExplicit` | explizites Euler-Verfahren | 1 | 1 | 0,18 | 0,21 s, **instabil** |
| `RungeKuttaSecondOrder` | Heun-Verfahren (explizite Trapezregel) | 2 | 2 | 0,73 | 0,36 s |
| `RungeKuttaClassic` | klassisches Runge-Kutta (RK4), **Standard** | 4 | 4 | 1,02 | 0,72 s |
| `RungeKuttaSecond` | Kutta-3/8-Regel | 4 | 4 | 1,02 | 0,72 s |
| `PredictorCorrectorHeun` | RK4-Prädiktor + iterierte implizite Trapezregel | 2 | ≈ 52 | 0,73 (Konvergenz) | 9,5 s |

- **Stabilitätsgrenze**: größtes `CFLReserve`, bei dem das Verfahren für $n = 5$, $N = 50$ stabil ist (Herleitung in [Abschnitt 4](#4-stabilität)). Der Standardwert 0,45 liegt nur beim Euler-Verfahren darüber.
- **Rechenzeit**: reine Simulation bis $t_1 = 20$ ms ohne Video (`EnablePlotting: false`), Mittel aus drei Läufen auf dem macOS-Entwicklungsrechner. Beim Euler-Verfahren enthält sie die Ausgabe einer Zeile pro Schritt, und die Lösung explodiert.

Die Namen `RungeKuttaSecond` und `RungeKuttaSecondOrder` sind leicht zu verwechseln: `RungeKuttaSecond` ist die *zweite RK4-Variante* (4. Ordnung), `RungeKuttaSecondOrder` das Verfahren *zweiter Ordnung* (Heun).

---

## 3 Die Verfahren

Notation: $X_k \approx X(t_k)$, $t_{k+1} = t_k + \Delta t$. Für die Stabilitätsbetrachtung wird jedes Verfahren auf die Testgleichung $\dot y = \lambda y$ angewendet; es liefert $y_{k+1} = R(z)\,y_k$ mit $z = \lambda\Delta t$. Stabil ist es, wenn $|R(z)| \le 1$ für alle Eigenwerte $\lambda$ von $L$ gilt.

### 3.1 Explizites Euler-Verfahren (`EulerExplicit`)

```math
X_{k+1} = X_k + \Delta t\,f(t_k, X_k),\qquad R(z) = 1 + z .
```

Code: `EulerExplicitClassic`. Ein Aufruf von `FluidMatrix`, dann `X += deltaT*k1`.

Das Stabilitätsgebiet ist die Kreisscheibe vom Radius 1 um $-1$. Es berührt die imaginäre Achse nur im Ursprung. Ein Teil der Eigenwerte des DG-Operators hat aber einen im Verhältnis zum Betrag kleinen Realteil (schwach gedämpfte Wellenmoden). Deshalb ist die Stabilitätsgrenze sehr klein und hängt vom Gitter ab: Für $n = 5$ beträgt sie 0,31 ($N = 25$), 0,18 ($N = 50$) und 0,10 ($N = 100$), fällt also mit feinerem Gitter weiter.

Mit dem Standardwert 0,45 ist das Verfahren **instabil**: Der größte Verstärkungsfaktor ist $|R| \approx 1{,}05$ pro Schritt, das ist ein Faktor $\approx 140$ alle 100 Schritte, bis die Lösung überläuft. Mit `CFLReserve` unter 0,18 läuft es auf dem Standardgitter, ist dann aber nur erster Ordnung genau und braucht sehr viele Schritte. Das Verfahren gibt außerdem in jedem Schritt eine Zeile `t=…` aus. Es eignet sich als Referenz, nicht für Rechnungen.

### 3.2 Heun-Verfahren (`RungeKuttaSecondOrder`)

```math
\begin{aligned}
k_1 &= f(t_k,\ X_k),\\
k_2 &= f(t_k + \Delta t,\ X_k + \Delta t\,k_1),\\
X_{k+1} &= X_k + \tfrac{\Delta t}{2}\,(k_1 + k_2),
\end{aligned}
\qquad
\begin{array}{c|cc}
0 & & \\
1 & 1 & \\ \hline
  & \tfrac12 & \tfrac12
\end{array}
\qquad
R(z) = 1 + z + \tfrac{z^2}{2}.
```

Code: `RungeKuttaSecondOrderHeun`. Explizite Trapezregel, zweite Ordnung, zwei Auswertungen.

Auch dieses Stabilitätsgebiet enthält kein Stück der imaginären Achse, denn $|R(iy)|^2 = 1 + y^4/4$. Die Überschreitung wächst aber nur mit der vierten Potenz von $y$, beim Euler-Verfahren dagegen quadratisch ($|1 + iy|^2 = 1 + y^2$). Für schwach gedämpfte Moden ist Heun deshalb viel großzügiger. Die Grenze liegt für $n = 5$ bei 0,83 / 0,73 / 0,69 ($N = 25 / 50 / 100$) und wird vom betragsgrößten, rein reellen Eigenwert bestimmt (Schnittpunkt $z = -2$).

Bei gleicher Zahl von Auswertungen ist Heun viel ungenauer als RK4: Heun mit `CFLReserve` 0,2 und RK4 mit 0,4 kosten gleich viel, der Fehler ist aber $6{,}6\cdot 10^{-3}$ gegenüber $4{,}1\cdot 10^{-5}$ ([Abschnitt 5](#5-genauigkeit)).

### 3.3 Klassisches Runge-Kutta-Verfahren (`RungeKuttaClassic`, Standard)

```math
\begin{aligned}
k_1 &= f(t_k,\ X_k), &
k_2 &= f(t_k + \tfrac{\Delta t}{2},\ X_k + \tfrac{\Delta t}{2}k_1),\\
k_3 &= f(t_k + \tfrac{\Delta t}{2},\ X_k + \tfrac{\Delta t}{2}k_2), &
k_4 &= f(t_k + \Delta t,\ X_k + \Delta t\,k_3),
\end{aligned}
\qquad
X_{k+1} = X_k + \tfrac{\Delta t}{6}\,(k_1 + 2k_2 + 2k_3 + k_4).
```

```math
\begin{array}{c|cccc}
0 & & & & \\
\tfrac12 & \tfrac12 & & & \\
\tfrac12 & 0 & \tfrac12 & & \\
1 & 0 & 0 & 1 & \\ \hline
  & \tfrac16 & \tfrac13 & \tfrac13 & \tfrac16
\end{array}
\qquad
R(z) = 1 + z + \frac{z^2}{2} + \frac{z^3}{6} + \frac{z^4}{24}.
```

Code: `RungeKuttaIntegrationClassic`. Vierte Ordnung, vier Auswertungen.

Das Stabilitätsgebiet enthält das Stück $|y| \le 2\sqrt2 \approx 2{,}83$ der imaginären Achse und reicht auf der reellen Achse bis $-2{,}785$. Beides passt gut zum Spektrum des DG-Operators. Die Grenze liegt bei 1,02 ($n = 5$, $N = 50$); der Standardwert 0,45 lässt mehr als den Faktor 2 Reserve. RK4 ist der Standard, weil es bei gleichem Aufwand mit Abstand den kleinsten Fehler hat.

### 3.4 Kutta-3/8-Regel (`RungeKuttaSecond`)

```math
\begin{aligned}
k_1 &= f(t_k,\ X_k), &
k_2 &= f(t_k + \tfrac{\Delta t}{3},\ X_k + \tfrac{\Delta t}{3}k_1),\\
k_3 &= f(t_k + \tfrac{2\Delta t}{3},\ X_k - \tfrac{\Delta t}{3}k_1 + \Delta t\,k_2), &
k_4 &= f(t_k + \Delta t,\ X_k + \Delta t\,(k_1 - k_2 + k_3)),
\end{aligned}
\qquad
X_{k+1} = X_k + \tfrac{\Delta t}{8}\,(k_1 + 3k_2 + 3k_3 + k_4).
```

```math
\begin{array}{c|cccc}
0 & & & & \\
\tfrac13 & \tfrac13 & & & \\
\tfrac23 & -\tfrac13 & 1 & & \\
1 & 1 & -1 & 1 & \\ \hline
  & \tfrac18 & \tfrac38 & \tfrac38 & \tfrac18
\end{array}
```

Code: `RungeKuttaIntegrationSecondVersion`. Ebenfalls vierte Ordnung mit vier Auswertungen.

Jedes explizite vierstufige Verfahren vierter Ordnung hat dieselbe Stabilitätsfunktion $R(z)$ wie RK4. Für dieses lineare Problem verhalten sich beide Verfahren daher praktisch gleich: Die gemessenen Fehler stimmen auf drei Stellen überein. Unterschiede zeigen sich erst bei nichtlinearen Problemen, über die Fehlerkonstanten; hier bietet die 3/8-Regel keinen Vorteil und dient dem Vergleich. Die Zeit wird in drei Dritteln aufaddiert, was sich nur in der letzten Stelle von $t$ bemerkbar macht.

### 3.5 Prädiktor-Korrektor nach Heun (`PredictorCorrectorHeun`)

Code: `PredictorCorrectorHeun`. Ablauf pro Schritt:

1. $X_\text{old} = X_k$, $k_1 = f(t_k, X_k)$.
2. **Prädiktor**: ein vollständiger RK4-Schritt liefert $X^{(0)}$ und setzt $t$ auf $t_{k+1}$. (RK4 berechnet $k_1$ intern noch einmal; der Aufruf in Schritt 1 ist überflüssig.)
3. **Korrektor**, wiederholt bis zur Konvergenz:
   ```math
   X^{(i+1)} = X_k + \frac{\Delta t}{2}\Big(k_1 + f\big(t_{k+1}, X^{(i)}\big)\Big),
   \qquad \text{Abbruch, wenn } \sum_j \big|X^{(i+1)}_j - X^{(i)}_j\big| < 10^{-12}.
   ```
4. Ausgabe einer Zeile `Convergence occured in Time : …` pro Schritt.

Der Fixpunkt der Korrektor-Iteration ist die **implizite Trapezregel** (Crank-Nicolson):

```math
X_{k+1} = X_k + \frac{\Delta t}{2}\Big(f(t_k, X_k) + f(t_{k+1}, X_{k+1})\Big),
\qquad R(z) = \frac{1 + z/2}{1 - z/2}.
```

Daraus folgt:

- **Ordnung 2, nicht 4.** Der RK4-Prädiktor liefert nur den Startwert; das Ergebnis ist die Trapezregel. Gemessen: Ordnung 1,99.
- **Die A-Stabilität geht verloren.** Die Trapezregel selbst ist A-stabil ($|R(z)| \le 1$ für $\mathrm{Re}\,z \le 0$, beliebig große Schritte). Die Fixpunktiteration konvergiert aber nur, wenn der Spektralradius $\rho\big(\tfrac{\Delta t}{2}L\big) < 1$ ist. Das ergibt dieselbe Schrittweitengrenze wie beim expliziten Heun-Verfahren: `CFLReserve` < 0,73 für $n = 5$, $N = 50$.
- **Hohe Kosten.** Bei 0,45 verkleinert jede Iteration den Fehler nur um den Faktor $\approx 0{,}61$. Gemessen sind es im Mittel 47 Korrektor-Iterationen pro Schritt (höchstens 61), zusammen mit dem Prädiktor ≈ 52 Auswertungen von $f$, 13-mal so viele wie RK4. Ergebnis: 9,5 s statt 0,72 s bei geringerer Genauigkeit.
- **Oberhalb der Konvergenzgrenze divergiert die Iteration.** Es gibt keine Iterationsgrenze. Gemessen bei `CFLReserve` 0,8: Residuum $2\cdot 10^{81}$ nach 2000 Iterationen. Die Schleife endet erst, wenn die Werte überlaufen (inf/NaN); das Ergebnis ist dann unbrauchbar.
- **Das Abbruchkriterium skaliert nicht mit der Problemgröße.** $10^{-12}$ ist eine absolute Schranke für die *Summe* über alle $m$ Freiheitsgrade. Mit wachsendem $m$ wächst das Rundungsrauschen dieser Summe mit. Gemessen mit einem glatten Zustand der Amplitude 2: bei $m = 20\,000$ ($N = 2000$) wird die Schranke noch erreicht (57 Iterationen), bei $m = 50\,000$ ($N = 5000$, $n = 5$) blieb das Residuum bei $1{,}3\cdot 10^{-12}$ stehen und erreichte die Schranke auch nach 2000 Iterationen nicht. Ohne Iterationsgrenze bliebe die Schleife dort hängen.

Das Verfahren ist im Menü als „slowest“ gekennzeichnet und eignet sich nur zum Vergleich. Eine implizite Trapezregel, die ihren Vorteil wirklich ausspielt, würde das lineare Gleichungssystem $(I - \tfrac{\Delta t}{2}L)\,X_{k+1} = (I + \tfrac{\Delta t}{2}L)\,X_k + \tfrac{\Delta t}{2}(g_k + g_{k+1})$ direkt lösen. $L$ ist konstant, die Matrix ließe sich also einmal faktorisieren.

### 3.6 Nicht verwendete Methoden

`ClassdXdt` deklariert außerdem `ButcherIntegration` (leerer Rumpf) und `RungeKuttaGill` (deklariert, nicht implementiert). Beide sind nicht auswählbar.

---

## 4 Stabilität

### 4.1 Vorgehen

$L$ wurde Spalte für Spalte aufgebaut, indem `FluidMatrix` bei $t = 0$ (dort ist $g = 0$) auf die Einheitsvektoren angewendet wurde. Danach wurden alle $m$ Eigenwerte berechnet. Für jedes Verfahren ist die Stabilitätsgrenze das größte `CFLReserve` (Schrittweite 0,0005), bei dem $\max_\lambda |R(\lambda\Delta t)| \le 1$ gilt. Für den Prädiktor-Korrektor ist es die Konvergenzbedingung $\rho(L)\,\Delta t/2 < 1$.

$\Delta t_1$ bezeichnet den Zeitschritt bei `CFLReserve` = 1, also $\Delta x/\big(c_0(2n-1)\big)$.

### 4.2 Grenzen in Abhängigkeit von der Ordnung ($N = 50$)

| $n$ (`order`) | $\rho(L)\,\Delta t_1$ | Euler | Heun | RK4 / 3/8 | Prädiktor-Korrektor |
|---|---|---|---|---|---|
| 1 | 1,71 | 1,17 | 1,17 | 1,63 | 1,17 |
| 2 | 1,69 | 0,72 | 1,18 | 1,65 | 1,18 |
| 3 | 2,04 | 0,39 | 0,98 | 1,36 | 0,98 |
| 5 | 2,73 | 0,18 | 0,73 | 1,02 | 0,73 |
| 8 | 3,66 | 0,086 | 0,55 | 0,76 | 0,55 |

### 4.3 Grenzen in Abhängigkeit vom Gitter ($n = 5$)

| $N$ | Euler | Heun | RK4 / 3/8 |
|---|---|---|---|
| 25 | 0,31 | 0,83 | 1,15 |
| 50 | 0,18 | 0,73 | 1,02 |
| 100 | 0,10 | 0,69 | 0,96 |

### 4.4 Folgerungen

- Mit dem Standardwert 0,45 sind Heun, RK4, die 3/8-Regel und der Prädiktor-Korrektor für alle geprüften Ordnungen stabil; RK4 hat überall mindestens rund den Faktor 1,7 Reserve.
- Der Faktor $1/(2n-1)$ in der Schrittweitenformel gleicht das Wachstum des Spektralradius mit der Ordnung nicht vollständig aus ($\rho\,\Delta t_1$ steigt von 1,69 auf 3,66). `CFLReserve` ist also nur ungefähr ordnungsunabhängig.
- Beim Euler-Verfahren fällt die Grenze mit feinerem Gitter weiter. Es gibt keinen festen `CFLReserve`-Wert, der für alle Gitter stabil ist.
- Die Grenzen beruhen auf den Eigenwerten. Der Upwind-DG-Operator ist nicht normal, daher sind sie eine notwendige Bedingung; ein Sicherheitsabstand wie beim Standardwert ist sinnvoll.

---

## 5 Genauigkeit

Zeitlicher Fehler für $n = 5$, $N = 50$ nach $T = 1$ ms, gemessen in der Maximumnorm über alle Freiheitsgrade gegen eine RK4-Referenzrechnung mit `CFLReserve` $= 0{,}45/32$. Der räumliche Fehler ist in Lösung und Referenz gleich und fällt heraus. $\Delta t$ wurde so gewählt, dass $T$ exakt getroffen wird.

| Verfahren | `CFLReserve` | Fehler | `CFLReserve` | Fehler | beobachtete Ordnung |
|---|---|---|---|---|---|
| Euler | 0,002 | 3,6·10⁻³ | 0,001 | 1,8·10⁻³ | 1,01 |
| Heun | 0,4 | 2,8·10⁻² | 0,2 | 6,6·10⁻³ | 2,10 |
| RK4 | 0,4 | 4,1·10⁻⁵ | 0,2 | 2,5·10⁻⁶ | 4,03 |
| 3/8-Regel | 0,4 | 4,1·10⁻⁵ | 0,2 | 2,5·10⁻⁶ | 4,03 |
| Prädiktor-Korrektor | 0,1 | 7,9·10⁻⁴ | 0,05 | 2,0·10⁻⁴ | 1,99 |

Mit dem Standardwert 0,45 hat RK4 einen Zeitfehler von $6{,}7\cdot 10^{-5}$. Der räumliche Fehler der Standardkonfiguration liegt bei etwa $10^{-6}$ ([`discontinuous-galerkin.md`](discontinuous-galerkin.md#10-genauigkeit-gemessen)). Die Zeitintegration bestimmt hier also die Genauigkeit. Wer genauer rechnen will, verkleinert zuerst `CFLReserve`: Der Fehler von RK4 fällt mit der vierten Potenz, die Rechenzeit steigt nur linear.

---

## 6 Empfehlung

- **RK4 (`RungeKuttaClassic`)** für alle normalen Rechnungen. Bei gleichem Aufwand ist es um Größenordnungen genauer als Heun und mit 0,45 sicher stabil.
- **Heun (`RungeKuttaSecondOrder`)** für schnelle Voransichten, wenn nur das qualitative Bild zählt: halbe Rechenzeit, aber ein mehrere hundertmal größerer Fehler.
- **3/8-Regel (`RungeKuttaSecond`)** bringt hier gegenüber RK4 nichts.
- **Euler (`EulerExplicit`)** nur zur Demonstration, mit deutlich kleinerem `CFLReserve` (Standardgitter: unter 0,18).
- **Prädiktor-Korrektor (`PredictorCorrectorHeun`)** nur zum Vergleich: 13-mal so teuer wie RK4, nur zweiter Ordnung und bei sehr großen Problemen ($m \gtrsim 50\,000$) ohne Abbruch der Iteration.

---

## Literatur

- E. Hairer, S. P. Nørsett, G. Wanner: *Solving Ordinary Differential Equations I. Nonstiff Problems.* 2. Aufl., Springer, 1993 (Runge-Kutta-Verfahren, Butcher-Tableaus, Stabilitätsfunktionen).
- E. Hairer, G. Wanner: *Solving Ordinary Differential Equations II. Stiff and Differential-Algebraic Problems.* 2. Aufl., Springer, 1996 (A-Stabilität, Trapezregel).
- J. S. Hesthaven, T. Warburton: *Nodal Discontinuous Galerkin Methods.* Springer, 2008 (Zeitintegration von DG-Verfahren).
