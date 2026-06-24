# CasioEmuMsvc MCP debugger

The MCP plugin exposes the runtime debugger over Streamable HTTP on the loopback
interface:

- MCP endpoint: `http://127.0.0.1:3001/mcp`
- Health check: `http://127.0.0.1:3001/health`
- Legacy SSE endpoint: `http://127.0.0.1:3001/sse`

The server only binds to `127.0.0.1`. The emulator must be running, and the
plugin DLL must be next to `CasioEmuMsvc.exe`.

## Build

```powershell
cmake -S . -B out\build\x64-Release -G "Visual Studio 17 2022" -A x64 -DBUILD_EXECUTABLE=ON
cmake --build out\build\x64-Release --config Release
```

`BUILD_MCP_PLUGIN` defaults to `ON` for the Windows executable build. The build
copies `CasioEmuMsvc.Plugin.McpPlugin.dll` next to the executable.

## Client configuration

Use a Streamable HTTP MCP client configuration equivalent to:

```json
{
  "mcpServers": {
    "casioemu": {
      "url": "http://127.0.0.1:3001/mcp"
    }
  }
}
```

## Debugger coverage

The tools are grouped around the existing debugger UI rather than maintaining a
separate debugger state:

| Debugger UI | MCP coverage |
| --- | --- |
| Code | status, pause/resume/reset, step into/over/out, disassembly, code read/write, execution breakpoints |
| Watch | register list/read/write, stack frames, textual backtrace, memory read/write |
| Breakpoints | memory read/write monitors, break-on-hit, hit count, hit PC/LR/call stack |
| Labels | label search |
| Variables | decoded real/complex calculator variables |
| Addrs | address watches and fixed-value overrides |
| Call Analysis | start/stop/clear recording and recorded calls |
| Snapshot | list/save/load/delete/import/export |
| Hardware | CPS, PD, interrupt, display/fading/residual/buffer/audio settings, screenshot, recording, ROM hot reload |
| Hex editors / Injector | generic MMU memory and ROM code reads/writes |
| Bitmap | raw memory reads; rendering is left to the MCP client |
| Keyboard | KI/KO matrix input, raw key code input, release all |

Theme editing, file dialogs, plugin logs, and the ROP source compiler are
application UI/development facilities rather than emulator debugger state and
are not exposed as MCP tools.

## Validation

The repository smoke test starts the supplied JP900CW model, initializes an MCP
session, checks core tool families, exercises snapshots and stepping, and then
stops the test process:

```powershell
powershell -ExecutionPolicy Bypass -File .\McpPlugin\test_mcp.ps1
```

The default model is:

`out\build\x64-Release\CasioEmuMsvc\Release\models\fx-JP900CW_emu`

