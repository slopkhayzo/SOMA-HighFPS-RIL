# SOMA.FPSFix

High-frame-rate presentation fix for the 64-bit Windows build of SOMA.

SOMA simulates at 60 Hz and normally draws at the same rate. This plugin lets
the game present at the display rate and interpolates what is drawn between
simulation ticks. Simulation timing, physics, scripts, AI, animation events,
and audio are not changed.

Version `1.0.0` covers:

- render unlock through the engine's own `LimitFPS` gate;
- camera interpolation, including matrix-mode authored cameras;
- presentation-rate gameplay mouse look with exact count conservation;
- first-person hands, held tools, and attached objects;
- rigid props of any body count, script-moved props, and skinned props;
- agent skeletal animation and skeleton-physics (ragdoll) props;
- menu, pause-menu, and terminal cursors.

Not covered: particles, screen effects, and mouse look with the game's Smooth
Mouse option enabled. Developed and tested against the GOG build only.

Runtime mappings use a strict wildcard scanner plus independent structural
validation. Missing or ambiguous evidence fails closed before a hook is
installed, and each feature has its own switch in `SOMA.FPSFix.ini`.

## Build

Use an x64 Visual Studio developer environment:

```powershell
cmake -S . -B build-x64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-x64 --parallel
ctest --test-dir build-x64 --output-on-failure
```

The first configure downloads the pinned MinHook dependency. Build output is
written to `build-x64/bin/`.

`package_plugin_only` produces the loader-independent archive. A
`package_with_loader` target is also generated when both
`ULTIMATE_ASI_LOADER_DLL` and `ULTIMATE_ASI_LOADER_LICENSE` point to a pinned
x64 loader and its matching license.

## Safety model

- SOMA mappings are accepted only after unique signature and structural checks.
- Hashes identify researched builds but are not compatibility allowlists.
- Behavioral features fail closed until implemented and validated.
- The plugin never changes simulation timing merely to match presentation.
- Reverse-engineering evidence and tools remain under the ignored `research/`
  directory.

See `research/docs/soma-high-frame-rate-plan.md` in the development workspace
for the investigation plan. That file is intentionally excluded from Git.


## License

SOMA.FPSFix is released under the [MIT License](LICENSE). It statically links
[MinHook](https://github.com/TsudaKageyu/minhook), which is distributed under
the 2-clause BSD license. The optional loader bundle includes the
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) under
its MIT license.
