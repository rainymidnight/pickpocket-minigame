# Pickpocket Minigame

SKSE plugin that replaces vanilla pickpocketing with a two-phase
KCD-like minigame:

- Phase 1: hold the activate key while targeting a pickpocketable NPC to bank
  time, with fill rate and risk based on player/target stats.
- Phase 2: spend that banked time in a circular inventory UI, reveal items by
  moving the cursor over slots, mark items to steal, then exit through the top
  node to transfer them.

## Requirements

- [Skyrim Script Extender (SKSE64)](https://skse.silverlock.org/)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
- [ImGui Icons](https://www.nexusmods.com/skyrimspecialedition/mods/114790)
- [Mesh Rendering Framework](https://www.nexusmods.com/skyrimspecialedition/mods/169708) (required for the `3D meshes` preview mode)
- [Perk Adjuster](https://www.nexusmods.com/skyrimspecialedition/mods/127999)
- [powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073)

## Build

Build requirements:

- Visual Studio 2022 C++ toolchain
- CMake, Ninja, and vcpkg
- Git

```powershell
cmake --preset release
cmake --build --preset release
```

## Packaging

```powershell
cmake --preset release
cmake --build --preset package
```