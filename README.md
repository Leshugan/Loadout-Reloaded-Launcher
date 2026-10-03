<p align="center">
  <img src="res/icon.png" alt="Loadout Reloaded" width="440">
</p>

<h1 align="center">Loadout Reloaded Launcher</h1>

<p align="center">
  <b>Русский</b> · <a href="README.en.md">English</a> · <a href="README.es.md">Español</a>
</p>

<p align="center">
  <a href="https://github.com/Leshugan/Loadout-Reloaded-Launcher/releases/latest"><b>⬇ Скачать LoadoutLauncher.exe</b></a>
</p>

Мой авторский лаунчер для проекта [Loadout Reloaded](https://gitlab.com/Bedebao/loadout-reloaded), который упрощает взаимодействие с игрой, с сервером, картами и скачиванием файлов игры.

Если вы ещё не качали игру или не заходили на страницу проекта, можете просто скачать этот лаунчер — вам будет предложено скачать игру, после чего всё будет просто работать без каких-либо настроек.


<p align="center">
  <img src="docs/ru_maps.jpg" alt="Главный экран" width="49%">
  <img src="docs/ru_settings.jpg" alt="Настройки" width="49%">
</p>

**Лицензия:** делайте с исходным кодом что хотите — форки, моды и всё остальное. Единственное условие — указывайте меня как автора.

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
