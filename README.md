# Project Erebus

Project Erebus is a native C++20/Qt 6 research simulator for investigating how
a hypothetical primordial black hole (PBH) could perturb the outer Solar
System. It combines direct Newtonian N-body integration with an interactive
OpenGL 3D view of planets, trans-Neptunian objects, orbital trajectories,
telemetry, and camera-relative gravitational-field visualisation.

## Physics model

The simulation uses SI units and direct Newtonian N-body gravity. Positions and
velocities are advanced with a time-reversible velocity-Verlet (leapfrog)
integrator. The PBH is a normal gravitating body with configurable mass and
bound Keplerian initial orbit. The default initial conditions are intentionally
simple circular heliocentric states; production studies should load a
validated ephemeris and uncertainty ensemble before drawing scientific
conclusions.

## Build

Dependencies:

- CMake 3.24+
- C++20 compiler
- Qt 6 (`Widgets`, `OpenGL`, `OpenGLWidgets`)
- Eigen 3.4+
- OpenMP (optional, used for larger body sets)
- HDF5 C++ (optional; reserved for experiment/result persistence)

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\erebus.exe
```

The viewport supports rotation, zoom, smooth double-click focus, Escape-to-
overview, live telemetry, orbital-element display, PBH scenario selection,
calculated trajectory trails, reference grids, axes, and a restrained
deep-space starfield. The renderer is intentionally lightweight and scientific:
body sizes are display-scaled, while all positions and trajectories remain in
SI units.

The **3D gravitational-field visualizer** evaluates the instantaneous Newtonian
potential
`Phi(r) = -sum(G*M_i/|r-r_i|)` for all massive bodies or one selected
contributor. Its camera-relative multi-well grid uses softened, bounded
displacement solely as a visual aid; it is not a relativistic spacetime model
and never modifies accelerations, trajectories, or conserved quantities.
Point-mass singularities use visualization-only softening. The rendering
approach is adapted from
[universe-simulator-cpp](https://github.com/ethanstoner/universe-simulator-cpp),
under its MIT license.

## Reference object presets

The initial reference snapshot uses a common application epoch of JD 2460840.5
(2026-06-09) and stores `a`, `e`, `i`, Omega, omega, and mean anomaly for:

- Sedna: 543.7115 AU, 0.8598806, 11.9253 deg, 144.5058 deg, 311.0993 deg,
  358.5957 deg
- 2012 VP113: 273.1359 AU, 0.7049256, 24.0007 deg, 90.8943 deg,
  294.2061 deg, 3.5869 deg
- 2015 TG387 (Goblin): 1389.0 AU, 0.9532, 11.67 deg, 300.98 deg,
  117.58 deg, 359.62 deg
- 2008 KV42 (Drac): 42.035 AU, 0.4968, 103.41 deg, 260.91 deg,
  132.61 deg, 341.23 deg

Values should be refreshed from the [JPL Small-Body Database][jpl-sbdb] or
Horizons before publication-grade analysis. The application keeps one explicit
epoch for imported presets and does not silently combine per-object epochs.
The PBH presets are hypothetical: arbitrary Omega, omega, and M are set to
zero and are clearly labelled in the UI.

[jpl-sbdb]: https://ssd.jpl.nasa.gov/tools/sbdb_lookup.html
