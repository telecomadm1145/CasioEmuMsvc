# CasioEmu Web

Build with Emscripten from the `CasioEmuMsvc` directory:

```powershell
.\web\build_web.ps1
```

The output is `build-web/CasioEmuWeb.html` plus its generated `.js`, `.wasm`, and `.data` files.

The browser build expects a model folder containing `config.bin` and the files referenced by that config, such as the interface image, ROM image, and flash image for fx-5800P models. The selected folder is copied into Emscripten's `/models/current` virtual path and persisted with IDBFS.

## Headless core build

Build the headless core API for browser frontends:

```powershell
.\web\build_core_web.ps1
```

To copy the generated files into another web project, pass its library output directory:

```powershell
.\web\build_core_web.ps1 -CopyTo <path-to-web-project-lib>
```

The output is `CasioEmuCore.js`, `CasioEmuCore.wasm`, and, when preloaded assets are emitted, `CasioEmuCore.data`.
This build does not use the SDL HTML shell. The host page loads the generated module and supplies the ROM bytes directly.
