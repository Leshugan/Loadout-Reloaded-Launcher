<p align="center">
  <img src="res/icon.png" alt="Loadout Reloaded" width="440">
</p>

<h1 align="center">Loadout Reloaded Launcher</h1>

<p align="center">
  <a href="README.md">Русский</a> · <b>English</b> · <a href="README.es.md">Español</a>
</p>

<p align="center">
  <a href="https://github.com/Leshugan/Loadout-Reloaded-Launcher/releases/latest"><b>⬇ Download LoadoutLauncher.exe</b></a>
</p>

My own launcher for the [Loadout Reloaded](https://gitlab.com/Bedebao/loadout-reloaded) project that makes it easier to work with the game, the server, maps and downloading the game files.

If you haven't downloaded the game yet or haven't visited the project page, you can simply download this launcher — it will offer to download the game, and after that everything will just work with no setup at all.


<p align="center">
  <img src="docs/en_maps_1001.jpg" alt="Main screen" width="49%">
  <img src="docs/en_settings_1001.jpg" alt="Settings" width="49%">
</p>

**License:** do whatever you want with the source code — forks, mods, anything. The only condition is to credit me as the author.

---

<details>
<summary>Build · Сборка · Compilación</summary>

Requires mingw-w64 (x86_64 and i686 `g++`). All libraries are in `third_party/`.

```sh
sh build.sh      # → out/LoadoutLauncher.exe
```

Large files in `res/*.llz` are packed with `tools/pack.py` and unpacked by the launcher at run time.

</details>

<details>
<summary>Third-party · Сторонние компоненты · Componentes de terceros</summary>

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [stb_image / stb_image_write](https://github.com/nothings/stb) — MIT / Public Domain
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD 2-Clause
- [miniz](https://github.com/richgel999/miniz) — MIT
- [Loadout Reloaded](https://gitlab.com/Bedebao/loadout-reloaded) — SSELoadout.dll and the uncensored patch file, AGPL-3.0
- SmartSteamEmu — files in `res/` are distributed as is
- Map images — from the game Loadout and the [Loadout Wiki](https://loadout.fandom.com); Loadout © Edge of Reality

</details>
