# Raumdiskretisierung: nodale Discontinuous-Galerkin-Methode

Dieses Dokument beschreibt, wie der Löser die eindimensionalen akustischen Gleichungen im Raum diskretisiert, und ordnet jeden Schritt dem Code zu. Das Ergebnis ist ein großes System gewöhnlicher Differentialgleichungen in der Zeit; wie es integriert wird, steht in [`zeitintegration.md`](zeitintegration.md).

Zahlenwerte beziehen sich auf die Standardkonfiguration in [`config_example.yaml`](../config_example.yaml): `order = 5`, `N = 50`, Gebiet $[0, 0{,}5]$ m, $c_0 = 300$ m/s, $\rho_0 = 1{,}5$ kg/m³, $f = 3$ kHz, `CFLReserve = 0.45`. Messwerte stammen aus einem Analyseprogramm, das den echten Operator aus `ClassdXdt` aufbaut (siehe [Abschnitt 10](#10-genauigkeit-gemessen)).

**Bezeichnungen**

| Symbol | Bedeutung | im Code |
|---|---|---|
| $N$ | Anzahl Zellen | `N` |
| $n$ | Knoten je Zelle | `order` |
| $p = n-1$ | Polynomgrad je Zelle | – |
| $\Delta x$ | Zellbreite | `dx` |
| $J = \Delta x/2$ | Jacobi-Determinante der Zellabbildung | `Jac`, `invJac` |
| $\xi_j,\ w_j$ | Gauß-Legendre-Knoten und -Gewichte auf $[-1,1]$ | `Stuetz`, `alpha` |
| $m = 2nN$ | Anzahl Freiheitsgrade | `X.n_elem` |

`order` zählt also Knoten, nicht den Polynomgrad: `order = 5` heißt Polynome vom Grad 4.

---

## 1 Modellgleichungen

Gelöst werden die linearisierten Gleichungen der Akustik für den Schalldruck $p(x,t)$ und die Schallschnelle $u(x,t)$:

```math
\frac{\partial p}{\partial t} + \rho_0 c_0^2\,\frac{\partial u}{\partial x} = 0,\qquad
\frac{\partial u}{\partial t} + \frac{1}{\rho_0}\,\frac{\partial p}{\partial x} = 0 .
```

In Vektorform mit $U = (p, u)^T$:

```math
\frac{\partial U}{\partial t} + A\,\frac{\partial U}{\partial x} = 0,\qquad
A = \begin{pmatrix} 0 & K \\ 1/\rho_0 & 0 \end{pmatrix},\qquad
K = \rho_0 c_0^2 = Z_0 c_0,\quad Z_0 = \rho_0 c_0 .
```

Im Code heißt $A$ `Funktionalmat` (`src/main.cpp`). Mit den Standardwerten ist $Z_0 = 450$ kg/(m²·s) und $K = 135\,000$ Pa.

### 1.1 Eigenstruktur und charakteristische Variablen

$A$ hat die Eigenwerte $\lambda_{1,2} = \pm c_0$. Mit der Eigenvektormatrix $R$ gilt $A = R\,\Lambda\,R^{-1}$:

```math
R = \begin{pmatrix} Z_0 & -Z_0 \\ 1 & 1 \end{pmatrix},\qquad
R^{-1} = \begin{pmatrix} \tfrac{1}{2Z_0} & \tfrac12 \\[2pt] -\tfrac{1}{2Z_0} & \tfrac12 \end{pmatrix},\qquad
\Lambda = \begin{pmatrix} c_0 & 0 \\ 0 & -c_0 \end{pmatrix}.
```

Die charakteristischen Variablen $W = R^{-1}U$ entkoppeln das System in zwei Transportgleichungen:

```math
w_1 = \frac{p + Z_0 u}{2Z_0}\ \ \text{(läuft mit } +c_0\text{ nach rechts)},\qquad
w_2 = \frac{-p + Z_0 u}{2Z_0}\ \ \text{(läuft mit } -c_0\text{ nach links)}.
```

Eine reine rechtslaufende Welle erfüllt also $p = Z_0 u$, eine linkslaufende $p = -Z_0 u$.

Für den Upwind-Fluss wird $A$ nach Laufrichtung aufgespalten, $A = A^+ + A^-$ mit $A^\pm = R\,\Lambda^\pm R^{-1}$, $\Lambda^+ = \mathrm{diag}(c_0, 0)$, $\Lambda^- = \mathrm{diag}(0, -c_0)$:

```math
A^+ = \frac{c_0}{2}\begin{pmatrix} 1 & Z_0 \\ 1/Z_0 & 1 \end{pmatrix},\qquad
A^- = \frac{c_0}{2}\begin{pmatrix} -1 & Z_0 \\ 1/Z_0 & -1 \end{pmatrix}.
```

Im Code: `R`, `R_inv`, `lambda_plus`, `lambda_minus` und daraus `Aplus`, `Aminus` in `main()`.

---

## 2 Gitter und Referenzelement

Das Gebiet $[x_0, x_1]$ wird in $N$ gleich breite Zellen $K_k = [x_k, x_{k+1}]$ zerlegt, $\Delta x = (x_1 - x_0)/N$. Jede Zelle wird affin auf das Referenzintervall $[-1, 1]$ abgebildet:

```math
x(\xi) = \frac{x_k + x_{k+1}}{2} + \frac{\Delta x}{2}\,\xi,\qquad
\mathrm{d}x = J\,\mathrm{d}\xi,\qquad
\frac{\mathrm{d}}{\mathrm{d}x} = \frac{1}{J}\,\frac{\mathrm{d}}{\mathrm{d}\xi},\qquad J = \frac{\Delta x}{2}.
```

Weil alle Zellen gleich groß sind, gelten Massen-, Steifigkeits- und Spurmatrizen für jede Zelle; sie werden nur einmal aufgebaut. Standard: $\Delta x = 1$ cm.

Die physikalischen Knotenpositionen `xnodes` sind $x(\xi_j)$ in jeder Zelle. Sie dienen nur der Ausgabe: das Video zeichnet die Knotenwerte, nicht das Polynom zwischen ihnen.

---

## 3 Nodale Basis auf Gauß-Legendre-Knoten

In jeder Zelle wird die Lösung durch ein Polynom vom Grad $p = n-1$ dargestellt, ausgedrückt in der Lagrange-Basis zu den $n$ Gauß-Legendre-Knoten $\xi_1 < \dots < \xi_n$:

```math
U_h(x,t)\big|_{K_k} = \sum_{j=1}^{n} U_{k,j}(t)\,\ell_j\big(\xi(x)\big),\qquad
\ell_j(\xi_i) = \delta_{ij}.
```

Die Unbekannten $U_{k,j} = (p_{k,j}, u_{k,j})^T$ sind damit direkt die Werte der Lösung an den Knoten. Zwischen den Zellen darf $U_h$ springen; das ist das „discontinuous“ in DG.

Die Gauß-Legendre-Knoten liegen im Innern von $(-1, 1)$, nicht auf den Zellrändern. Randwerte müssen deshalb aus allen Knoten der Zelle extrapoliert werden (Spuroperator, [Abschnitt 4.3](#43-spuroperator)).

**Berechnung der Knoten und Gewichte** (`src/vandermonde.cpp`, `GaussLegendre`):

- $n = 1$: ein Knoten in der Zellmitte, $\xi = 0$, $w = 2$ (fest im Code). Die Basis ist konstant, $\mathbf Q = 0$, und das Verfahren reduziert sich auf das Upwind-Finite-Volumen-Verfahren erster Ordnung, das die Wellenfront deutlich stärker verschmiert als die höheren Ordnungen ([Abschnitt 10.2](#102-ordnung-1-verschmierte-wellenfront)).
- $n = 2$: $\xi = \pm 1/\sqrt{3}$, $w = 1, 1$ (fest im Code).
- $n = 5$: $\xi = 0,\ \pm\tfrac13\sqrt{5 \mp 2\sqrt{10/7}}$, $w = \tfrac{128}{225},\ \tfrac{322 \pm 13\sqrt{70}}{900}$ (fest im Code).
- alle anderen $n \ge 3$: Golub-Welsch-Verfahren. Die Knoten sind die Eigenwerte der symmetrischen Tridiagonalmatrix (Jacobi-Matrix) mit Nulldiagonale und Nebendiagonale $\beta_k = k/\sqrt{4k^2-1}$, die Gewichte $w_j = 2\,v_{1j}^2$ aus der ersten Komponente des normierten Eigenvektors.

---

## 4 Schwache Formulierung und semidiskretes System

Die Gleichung wird in Zelle $K_k$ mit jeder Basisfunktion $\ell_i$ multipliziert, integriert und der Flussterm partiell integriert:

```math
\int_{K_k} \ell_i\,\frac{\partial U_h}{\partial t}\,\mathrm{d}x
- \int_{K_k} \frac{\mathrm{d}\ell_i}{\mathrm{d}x}\,A\,U_h\,\mathrm{d}x
+ \Big[\ell_i\,F^*\Big]_{x_k}^{x_{k+1}} = 0 .
```

An den Zellrändern ist $A U_h$ wegen der Sprünge nicht eindeutig. Dort steht der **numerische Fluss** $F^*$, der beide Nachbarzustände verwendet ([Abschnitt 5](#5-numerischer-fluss-upwind)). Er ist die einzige Kopplung zwischen den Zellen.

Auf das Referenzelement transformiert und für alle $i$ zusammengefasst ergibt sich pro Zelle:

```math
J\,\mathbf{M}\,\frac{\mathrm{d}\mathbf{U}_k}{\mathrm{d}t}
= \mathbf{Q}\,\mathbf{U}_k \;-\; \boldsymbol{\psi}_R\,F^*_{k+1/2} \;+\; \boldsymbol{\psi}_L\,F^*_{k-1/2},
```

wobei $\mathbf{U}_k \in \mathbb{R}^{2n}$ alle Knotenwerte der Zelle enthält. Aufgelöst:

```math
\frac{\mathrm{d}\mathbf{U}_k}{\mathrm{d}t}
= \frac{1}{J}\,\mathbf{M}^{-1}\Big(\mathbf{Q}\,\mathbf{U}_k + \mathbf{F}_k\Big),\qquad
\mathbf{F}_k = -\boldsymbol{\psi}_R\,F^*_{k+1/2} + \boldsymbol{\psi}_L\,F^*_{k-1/2}.
```

Das ist wörtlich die letzte Zeile von `ClassdXdt::FluidMatrix`:

```cpp
dXdt = invMdiag % (vectorise(Q * reshape(X, 2*order, N)) + Flux);
dXdt *= invJac;
```

Für das gekoppelte $(p,u)$-System haben alle Matrizen Kronecker-Struktur: skalare $n\times n$-Matrizen der Basis, kombiniert mit $2\times 2$-Blöcken für die beiden Felder.

### 4.1 Massenmatrix

```math
M_{ij} = \int_{-1}^{1} \ell_i(\xi)\,\ell_j(\xi)\,\mathrm{d}\xi .
```

Die $n$-Punkt-Gauß-Legendre-Quadratur ist exakt für Polynome bis Grad $2n-1$. Der Integrand hat Grad $2n-2$, die Quadratur ist also exakt, und weil $\ell_i(\xi_q)\ell_j(\xi_q) = \delta_{iq}\delta_{jq}$ gilt:

```math
\mathbf{M} = \mathrm{diag}(w_1, \dots, w_n)\quad\text{(exakt, keine Näherung)}.
```

Bei Gauß-Lobatto-Knoten (mit Randpunkten, z. B. bei Hesthaven/Warburton) wäre die Diagonalform nur eine Quadraturnäherung („mass lumping“). Hier ist sie exakt, und $\mathbf{M}^{-1}$ ist eine elementweise Skalierung: `invMdiag` enthält $1/w_j$, je zweimal (für $p$ und $u$) und für alle $N$ Zellen wiederholt.

### 4.2 Steifigkeitsmatrix

```math
S_{ij} = \int_{-1}^{1} \ell_i'(\xi)\,\ell_j(\xi)\,\mathrm{d}\xi
= \sum_{q} w_q\,\ell_i'(\xi_q)\,\ell_j(\xi_q) = w_j\,\ell_i'(\xi_j)
\quad\Longrightarrow\quad \mathbf{S} = \mathbf{D}^T\mathbf{W}.
```

Der Integrand hat Grad $2n-3$, die Quadratur ist wieder exakt. Dabei ist $\mathbf{W} = \mathrm{diag}(w)$ und $\mathbf{D}$ die nodale Differentiationsmatrix, $D_{qi} = \ell_i'(\xi_q)$.

`TransposeDiffMatrix` berechnet $\mathbf{D}$ über die Monombasis: Mit der Vandermonde-Matrix $V_{ij} = \xi_i^{\,j}$ und ihrer Ableitung $\tilde V_{ij} = j\,\xi_i^{\,j-1}$ ist $\ell_i(\xi) = \sum_m (V^{-1})_{mi}\,\xi^m$, also

```math
\mathbf{D} = \tilde V\,V^{-1}.
```

Der Volumenoperator für das $(p,u)$-System ist

```math
\mathbf{Q} = (\mathbf{D}^T\mathbf{W}) \otimes A ,
```

im Code als `kron(eye(order), A) * kron(DT, eye(2)) * M` aufgebaut (die Faktoren vertauschen, das Produkt ist dasselbe). Für $n = 2$ steht das Ergebnis fest im Code: $\mathbf{D}^T = \tfrac{\sqrt3}{2}\begin{pmatrix}-1 & -1\\ 1 & 1\end{pmatrix}$, $\mathbf{W} = I$.

### 4.3 Spuroperator

Die Randwerte einer Zelle sind Linearkombinationen ihrer Knotenwerte:

```math
U_h(\xi = -1) = \boldsymbol{\psi}_L^T\,\mathbf{U}_k,\qquad
U_h(\xi = +1) = \boldsymbol{\psi}_R^T\,\mathbf{U}_k,\qquad
\boldsymbol{\psi}_{L} = \boldsymbol{\ell}(-1)\otimes I_2,\quad
\boldsymbol{\psi}_{R} = \boldsymbol{\ell}(+1)\otimes I_2 .
```

$\boldsymbol\psi_L, \boldsymbol\psi_R$ sind $2n\times 2$-Matrizen. Transponiert holen sie den Randwert aus der Zelle, untransponiert verteilen sie den Randfluss auf die Knoten. Im Code ist `psi` eine $2n\times 4$-Matrix, Spalten 0–1 = links (`psiLeft`), 2–3 = rechts (`psiRight`); für $n = 2$ und $n = 5$ fest eingetragen, sonst über `LagrangeAtPoint` berechnet.

---

## 5 Numerischer Fluss (Upwind)

An der Grenzfläche $x_{k+1/2}$ zwischen Zelle $k$ und $k+1$ seien

```math
U^- = \boldsymbol\psi_R^T\,\mathbf{U}_k\quad\text{(Randwert der linken Zelle)},\qquad
U^+ = \boldsymbol\psi_L^T\,\mathbf{U}_{k+1}\quad\text{(Randwert der rechten Zelle)}.
```

Der Fluss nimmt jede Charakteristik von der Seite, von der sie kommt:

```math
F^*(U^-, U^+) = A^+\,U^- + A^-\,U^+ .
```

Für ein lineares System ist das die exakte Lösung des Riemann-Problems (Godunov-Fluss). Ausgeschrieben ist $F^* = A\,U^*$ mit dem Grenzflächenzustand

```math
p^* = \frac{p^- + p^+}{2} + Z_0\,\frac{u^- - u^+}{2},\qquad
u^* = \frac{u^- + u^+}{2} + \frac{p^- - p^+}{2Z_0}.
```

Der Fluss ist konsistent ($F^*(U,U) = AU$) und dämpft Sprünge zwischen den Zellen. Diese numerische Dissipation und der absorbierende linke Rand sorgen dafür, dass alle Eigenwerte des semidiskreten Operators einen negativen Realteil haben (gemessen für die Standardkonfiguration: $\max \mathrm{Re}\,\lambda \approx -9{,}8\cdot 10^3\ \mathrm{s}^{-1}$).

Im Code: `ClassdXdt::numericalFlux(Ul, Ur)` bekommt die vollständigen Knotenvektoren beider Zellen und bildet die Spuren selbst. `FluidMatrix` setzt daraus $\mathbf F_k$ zusammen:

```text
Flux(Zelle k) = -psiRight * numericalFlux(U(k),   U(k+1))    // rechter Rand der Zelle
                +psiLeft  * numericalFlux(U(k-1), U(k))      // linker Rand der Zelle
```

Jeder innere Grenzflächenfluss wird dabei zweimal berechnet, einmal von jeder Nachbarzelle. Das kostet doppelte Arbeit im Flussteil, macht die Zellschleife aber ohne Abhängigkeiten zwischen Iterationen und damit direkt mit OpenMP parallelisierbar.

---

## 6 Randbedingungen

Beide Ränder werden über einen **Geisterzustand** außerhalb des Gebiets realisiert, der in den normalen numerischen Fluss eingesetzt wird.

### 6.1 Linker Rand: einlaufende Sinuswelle, nicht reflektierend

Der Geisterzustand links ist

```math
U_g(t) = \begin{pmatrix} 1 \\ 1/Z_0 \end{pmatrix}\sin(\omega t),\qquad \omega = 2\pi f .
```

Im Code ist das `leftInflowState * sin(omega*t)`, ein Knotenvektor mit diesem Wert an allen $n$ Knoten; weil $\sum_j \ell_j \equiv 1$, ist seine Spur genau $U_g(t)$. In charakteristischen Variablen: $w_1 = \sin(\omega t)/Z_0$, $w_2 = 0$. Der Geisterzustand enthält also nur eine rechtslaufende Welle mit der Druckamplitude 1 Pa.

Am linken Rand gilt $F^* = A^+ U_g + A^- U^+$. Der Fluss übernimmt aus dem Geisterzustand nur die einlaufende Charakteristik; die auslaufende (linkslaufende) kommt aus dem Gebiet und verlässt es ungehindert. Der Rand ist damit gleichzeitig Quelle und **transparent** für die an der Wand reflektierte Welle.

### 6.2 Rechter Rand: schallharte Wand

Der Geisterzustand rechts ist das Spiegelbild der letzten Zelle mit umgedrehter Schnelle:

```math
\mathbf U_g = (P_n \otimes \mathrm{diag}(1,-1))\,\mathbf U_N,\qquad
p_g(\xi) = p(-\xi),\quad u_g(\xi) = -u(-\xi),
```

mit der Matrix $P_n$, die die Reihenfolge der Knoten umkehrt (`rightWallReflection = kron(fliplr(eye(order)), diag(1,-1))`). Weil die Gauß-Legendre-Knoten symmetrisch liegen, ist die Spur des Geisterzustands $U^+ = (p^-, -u^-)$. Eingesetzt in den Riemann-Zustand:

```math
u^* = 0,\qquad p^* = p^- + Z_0 u^- = 2 Z_0\,w_1^- .
```

Die Schnelle an der Wand verschwindet (starre Wand), und der Druck an der Wand ist das Doppelte der einlaufenden Welle: Reflexionsfaktor $+1$ für den Druck.

### 6.3 Was physikalisch passiert

Die Wellenlänge ist $\lambda = c_0/f = 0{,}1$ m, das Gebiet ist 5 Wellenlängen lang. Die Wellenfront erreicht die Wand nach $L/c_0 \approx 1{,}67$ ms, die reflektierte Front ist nach $\approx 3{,}33$ ms zurück am linken Rand und verlässt dort das Gebiet. Ab dann überlagern sich ein- und rücklaufende Welle zu einer **stehenden Welle** mit Druckamplitude 2, Druckbauch an der Wand und Druckknoten im Abstand $\lambda/4 + j\lambda/2$ von der Wand ([Bild](standing-wave.png)).

---

## 7 Anfangsbedingung

Der Code startet mit $U(x, 0) = 0$ (`InitialAndBoundary::U_init(ui, order)`). Die Welle entsteht allein durch den linken Rand. Eine Gauß-Anfangsverteilung ist vorhanden (`U_init(xi, ui)`), in `main.cpp` aber auskommentiert; die Konfigurationswerte `mu`, `sigma` und `height` werden zwar gelesen, wirken sich derzeit aber nicht aus.

Weil $\sin(0) = 0$, setzt die einlaufende Welle stetig ein, aber mit einem Knick (die Zeitableitung springt bei $t = 0$). Dieser Knick wandert als Wellenfront durch das Gebiet und verursacht dort kleine lokale Oszillationen; hinter der Front ist die Lösung glatt.

---

## 8 Datenlayout und Implementierung

Der Zustandsvektor `X` hat $m = 2nN$ Einträge (Standard: 500). Die Werte liegen knotenweise verschränkt, Zelle für Zelle:

```text
X = [ p(1,1) u(1,1)  p(1,2) u(1,2)  …  p(1,n) u(1,n) | p(2,1) u(2,1) … | … | p(N,n) u(N,n) ]
       └────────────── Zelle 1: 2n Werte ───────────┘
```

Index $2(kn + j)$ (0-basiert) ist $p$ am Knoten $j$ in Zelle $k$, der nächste Index $u$. Das Layout entsteht aus `vectorise(U0)` einer $2\times nN$-Matrix in Armadillos spaltenweiser Speicherung. Für Ausgabe und Video werden nur die geraden Indizes (Druck) gespeichert (`pressureDofs` in `main.cpp`).

Eine Auswertung `FluidMatrix(X, dXdt, t)`:

1. **Flussvektor**: erste Zelle (Einstrom links + rechter Nachbar), letzte Zelle (linker Nachbar + Wand), dann die inneren Zellen $k = 1 \dots N-2$ in einer Schleife. Die Schleife läuft mit OpenMP parallel, sobald $N \ge$ `OpenMPMinCells`. Aufwand $O(Nn)$.
2. **Volumenterm**: `Q * reshape(X, 2n, N)` wendet $\mathbf Q$ auf alle Zellen in einer einzigen Matrixmultiplikation $(2n\times 2n)\cdot(2n\times N)$ an, statt die blockdiagonale Matrix $I_N\otimes\mathbf Q$ aufzubauen. Aufwand $O(Nn^2)$.
3. **Skalierung** mit $\mathbf M^{-1}$ (`invMdiag`) und $1/J$.

Für $n = 1$ und $n = 2$ gibt es eigene Zweige in `FluidMatrix` mit fest kodierten Indizes, mathematisch aber identisch zum allgemeinen Zweig. `main()` verlangt $N \ge 2$, weil die innere Schleife sonst unterläuft.

---

## 9 Zeitschritt (CFL-Bedingung)

```math
\Delta t = \texttt{CFLReserve}\cdot\frac{\Delta x}{c_0\,(2n-1)} = \texttt{CFLReserve}\cdot\frac{\Delta x}{c_0\,(2p+1)} .
```

Der Faktor $1/(2p+1)$ ist die übliche Faustregel für Runge-Kutta-DG-Verfahren (Cockburn/Shu): Der Spektralradius des DG-Operators wächst mit dem Polynomgrad, der stabile Zeitschritt schrumpft entsprechend. `CFLReserve` soll dadurch eine Sicherheitsfraktion sein, die für alle Ordnungen ungefähr gleich viel Abstand zur Stabilitätsgrenze lässt.

Gemessen gleicht der Faktor das Wachstum nicht vollständig aus: Die praktisch nutzbare Grenze von RK4 in `CFLReserve` fällt von 1,43 ($n = 2$) über 0,92 ($n = 5$) auf 0,69 ($n = 8$) und 0,37 ($n = 20$). Nach den Eigenwerten allein wären es für die ersten drei 1,65, 1,02 und 0,76. Mit 0,45 bleibt RK4 bis Ordnung 15 darunter; ab Ordnung 16 muss `CFLReserve` kleiner sein. Die vollständigen Tabellen für alle Integratoren und der Grund für den Unterschied stehen in [`zeitintegration.md`](zeitintegration.md#4-stabilität).

Standardwerte: $\Delta t = 0{,}45 \cdot 0{,}01/(300\cdot 9) \approx 1{,}67\ \mu$s, rund 12 000 Schritte bis $t_1 = 20$ ms und 200 Schritte pro Schwingungsperiode.

---

## 10 Genauigkeit (gemessen)

Die Messungen in diesem Abschnitt stammen aus Analyseprogrammen, die nicht Teil des Repositorys sind. Sie bauen Massenmatrix, Steifigkeitsmatrix und Flüsse genau wie `main()` auf und rufen dann `ClassdXdt::FluidMatrix` und die Integratoren direkt auf. Die Profile in [Abschnitt 10.2](#102-ordnung-1-verschmierte-wellenfront) stammen aus einer Kopie von `main()`, die zusätzlich das Druckprofil an den Knoten ausgibt.

### 10.1 Konvergenzordnung

Für glatte Lösungen konvergiert DG mit Upwind-Fluss mit der Ordnung $p + 1 = n$ im Raum. Geprüft wurde das gegen die exakte einlaufende Welle $p(x,t) = \sin\big(\omega(t - x/c_0)\big)$: maximale Abweichung an den Druckknoten mit $x < 0{,}1$ m bei $t = 0{,}5$ ms (Wellenfront bei 0,15 m, noch keine Reflexion). Die Zeitintegration lief mit RK4 und `CFLReserve = 0.02`, damit der Zeitfehler keine Rolle spielt.

| $n$ | $N = 10$ | $N = 20$ | $N = 40$ | $N = 80$ | beobachtete Ordnung |
|---|---|---|---|---|---|
| 2 | 3,7·10⁻¹ | 2,5·10⁻¹ | 5,6·10⁻² | 1,1·10⁻² | ≈ 2,2–2,3 |
| 3 | 1,7·10⁻¹ | 1,9·10⁻² | 2,1·10⁻³ | 2,5·10⁻⁴ | ≈ 3,1 |
| 5 | 2,2·10⁻³ | 1,4·10⁻⁴ | 3,6·10⁻⁶ | 1,1·10⁻⁷ | ≈ 5,0 |

Die Ordnungen entsprechen der Theorie. Bei $N = 10$ (zwei Zellen pro Wellenlänge) ist $n = 2$ noch nicht im asymptotischen Bereich. Für die Standardkonfiguration ($n = 5$, $N = 50$, 10 Zellen bzw. 50 Knoten pro Wellenlänge) liegt der Raumfehler bei etwa $10^{-6}$. Mit `CFLReserve = 0.45` ist der Zeitfehler von RK4 mit etwa $7\cdot 10^{-5}$ deutlich größer (siehe [`zeitintegration.md`](zeitintegration.md#5-genauigkeit)).

### 10.2 Ordnung 1: verschmierte Wellenfront

Mit $n = 1$ ist das Verfahren das Upwind-Finite-Volumen-Verfahren erster Ordnung. Es transportiert die Welle mit der richtigen Geschwindigkeit, verschmiert die Wellenfront aber stark. Misst man die Front über eine Schwelle (größtes $x$ mit $|p| \ge 0{,}05$), liegt sie deshalb vor $c_0 t$: bei $N = 1600$ und $t = 0{,}5$ ms zum Beispiel bei 0,155 m statt 0,150 m.

**Mechanismus.** Solange keine linkslaufende Welle existiert (vor der Reflexion an der Wand), gilt $p = Z_0 w_1$, und das Verfahren lautet für die Zellwerte

```math
\frac{\mathrm{d}p_j}{\mathrm{d}t} = \frac{c_0}{\Delta x}\,\big(p_{j-1} - p_j\big),\qquad j = 1, \dots, N,\qquad p_0(t) = \sin(\omega t).
```

Jede Zelle wirkt wie ein Tiefpass erster Ordnung mit der Zeitkonstante $\Delta x/c_0$. Das Randsignal erreicht Zelle $j$ mit einer Erlang-verteilten Verzögerung: im Mittel nach $j\,\Delta x/c_0$, also mit der richtigen Geschwindigkeit, aber gestreut um $\sqrt{j}\,\Delta x/c_0$. Im Raum entspricht das einer Verschmierung der Breite

```math
\sigma = \sqrt{c_0\,\Delta x\,t}\,.
```

Dasselbe Ergebnis liefert die modifizierte Gleichung des Upwind-Verfahrens (Warming/Hyett). In führender Ordnung löst das Verfahren nicht die reine Transportgleichung, sondern

```math
\frac{\partial p}{\partial t} + c_0\,\frac{\partial p}{\partial x} = D\,\frac{\partial^2 p}{\partial x^2},\qquad D = \frac{c_0\,\Delta x}{2},\qquad \sigma^2 = 2Dt .
```

Die exakte Lösung hat an der Front $x = c_0 t$ einen Knick: davor ist $p = 0$, dahinter steigt der Sinus an. Die Diffusion glättet den Knick nach beiden Seiten, dadurch reicht ein Ausläufer des Signals vor die Front. Eine Schwellenmessung findet die „Front“ in diesem Ausläufer. Den Knick selbst enthält die verschmierte Lösung nicht mehr.

**Vergleich mit der exakten Lösung des Verfahrens.** Die Kettengleichung oben lässt sich geschlossen lösen:

```math
p_j(t) = \mathrm{Im}\Big[e^{i\omega t}\Big(\frac{r}{r + i\omega}\Big)^{j}\,P\big(j,\,(r + i\omega)\,t\big)\Big],\qquad
r = \frac{c_0}{\Delta x},\qquad
P(j, z) = 1 - e^{-z}\sum_{m=0}^{j-1}\frac{z^m}{m!} .
```

Die Simulation (RK4, `CFLReserve = 0.45`) weicht davon höchstens um $4\cdot 10^{-6}$ ab; das ist der Zeitfehler von RK4. Der Code rechnet also genau das Upwind-Verfahren erster Ordnung, und die Verschmierung ist eine Eigenschaft dieses Verfahrens.

**Geschwindigkeit.**

| $N$ | $\Delta x$ | Schwerpunkt eines Gauß-Pulses | Phasengeschwindigkeit | Nulldurchgänge, max. Abweichung |
|---|---|---|---|---|
| 400 | 1,25 mm | 300,000000 m/s | 300,56 m/s | 0,57 mm |
| 1600 | 0,31 mm | 300,000000 m/s | 300,04 m/s | 0,15 mm |
| 6400 | 0,078 mm | 300,000000 m/s | 300,002 m/s | 0,04 mm |

- *Puls*: rechtslaufender Gauß-Puls ($p = Z_0 u$, Standardabweichung 1 cm, Start bei 0,1 m) ohne Randanregung, Schwerpunkt mit der Quadratur des Verfahrens bis 0,8 ms gemessen. Der Schwerpunkt ist unempfindlich gegen Verschmierung und wandert exakt mit $c_0$.
- *Phasengeschwindigkeit*: Lage des Nulldurchgangs $x = c_0 t - \lambda/2$ bei $t$ = 0,25, 0,5 und 1 ms, lineare Regression. Die Abweichung ist der Dispersionsfehler des Verfahrens und fällt quadratisch mit $\Delta x$.
- *Nulldurchgänge*: größte Abweichung aller Nulldurchgänge hinter der Front von $c_0 t - m\lambda/2$ zu den drei Zeiten. Sie beträgt höchstens etwa $\Delta x/2$ und fällt linear mit $\Delta x$.

**Verschmierung.** Die Varianz des Pulses wächst um genau $c_0\,\Delta x\,t$ (bei $N = 400$ nach 0,2 ms: gemessen und berechnet $7{,}523437\cdot 10^{-5}$ m²). Bei Ordnung 5 bleibt sie konstant (Änderung unter $10^{-15}$ m²).

Druckprofil an der Front bei $t = 0{,}5$ ms:

| $x$ [m] | exakt | $N = 400$ | $N = 1600$ | $N = 6400$ |
|---|---|---|---|---|
| 0,140 | 0,588 | 0,512 | 0,548 | 0,574 |
| 0,145 | 0,309 | 0,385 | 0,334 | 0,307 |
| 0,150 | 0 | 0,267 | 0,159 | 0,084 |
| 0,155 | 0 | 0,170 | 0,055 | 0,007 |
| 0,160 | 0 | 0,099 | 0,014 | 0,000 |
| 0,170 | 0 | 0,026 | 0,000 | 0,000 |

Schwellenfront bei $t = 0{,}5$ ms, verglichen mit einem einfachen Diffusionsmodell (exakte Lösung, mit einer Gaußglocke der Breite $\sigma$ geglättet):

| $N$ | $\sigma$ | Front gemessen | Diffusionsmodell | Versatz |
|---|---|---|---|---|
| 400 | 13,7 mm | 0,1654 m | 0,1656 m | +16,0 mm |
| 1600 | 6,9 mm | 0,1554 m | 0,1555 m | +6,2 mm |
| 6400 | 3,4 mm | 0,1513 m | 0,1513 m | +2,1 mm |

Auch die exakte Lösung erreicht die Schwelle erst 0,8 mm hinter dem Knick, bei rund 0,149 m; der Versatz bezieht sich auf diesen Wert.

Weitere Beobachtungen:

- Bei $N = 400$ wächst der Versatz mit der Zeit: 10 mm bei 0,25 ms, 16 mm bei 0,5 ms, 25 mm bei 1 ms. Die Nulldurchgänge bleiben dabei an ihrer Stelle.
- Dieselbe Diffusion dämpft die Welle mit dem Faktor $e^{-Dk^2x/c_0}$, $k = \omega/c_0$. Bei $N = 400$ und $x = 0{,}12$ m ist $p = 0{,}71$ statt 0,95; das Modell sagt 0,71 voraus.

**Folgerung.** Die Verschmierung schrumpft nur mit $\sqrt{\Delta x}$: Viermal so viele Zellen halbieren sie. Die höheren Ordnungen verschmieren den Knick kaum. Mit derselben Schwellenmessung liegt die Front bei Ordnung 2 ($N = 100$) 2,8 mm und bei Ordnung 5 ($N = 50$) 0,4 mm vor dem exakten Wert.

---

## 11 Bekannte Einschränkungen

- **Monom-Vandermonde-Matrix.** $\mathbf D$ wird über $V^{-1}$ in der Monombasis berechnet. Die Kondition von $V$ wächst schnell mit $n$: etwa 23 ($n = 5$), 300 ($n = 8$), $10^4$ ($n = 12$), $10^7$ ($n = 20$). Bis $n \approx 12$ ist das unkritisch; für sehr hohe Ordnungen wäre eine Konstruktion über Legendre-Polynome (orthogonale Vandermonde-Matrix) genauer.
- **Gauß-Anfangsbedingung inaktiv**, siehe [Abschnitt 7](#7-anfangsbedingung).

---

## Literatur

- J. S. Hesthaven, T. Warburton: *Nodal Discontinuous Galerkin Methods. Algorithms, Analysis, and Applications.* Springer, 2008.
- B. Cockburn, C.-W. Shu: *Runge–Kutta Discontinuous Galerkin Methods for Convection-Dominated Problems.* J. Sci. Comput. 16 (2001), 173–261.
- R. J. LeVeque: *Finite Volume Methods for Hyperbolic Problems.* Cambridge University Press, 2002 (Riemann-Problem und Upwind-Fluss für lineare Systeme, modifizierte Gleichungen).
- R. F. Warming, B. J. Hyett: *The Modified Equation Approach to the Stability and Accuracy Analysis of Finite-Difference Methods.* J. Comput. Phys. 14 (1974), 159–179.
