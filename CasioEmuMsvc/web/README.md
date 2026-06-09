# CasioEmu Web

Build with Emscripten from the `CasioEmuMsvc` directory:

```powershell
.\web\build_web.ps1
```

The output is `build-web/CasioEmuWeb.html` plus its generated `.js`, `.wasm`, and `.data` files.

The browser build expects a model folder containing `config.bin` and the files referenced by that config, such as the interface image, ROM image, and flash image for fx-5800P models. The selected folder is copied into Emscripten's `/models/current` virtual path and persisted with IDBFS.
