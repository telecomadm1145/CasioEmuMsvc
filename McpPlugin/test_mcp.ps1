param(
    [string]$ReleaseDirectory = "",
    [string]$Model = "models\fx-JP900CW_emu",
    [int]$Port = 3001
)

$ErrorActionPreference = "Stop"

if (-not $ReleaseDirectory) {
    $ReleaseDirectory = Join-Path $PSScriptRoot "..\out\build\x64-Release\CasioEmuMsvc\Release"
}
$ReleaseDirectory = (Resolve-Path $ReleaseDirectory).Path
$exe = Join-Path $ReleaseDirectory "CasioEmuMsvc.exe"
$baseUri = "http://127.0.0.1:$Port"
$mcpUri = "$baseUri/mcp"
$process = $null

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

try {
    $process = Start-Process -FilePath $exe `
        -ArgumentList $Model, "paused=1" `
        -WorkingDirectory $ReleaseDirectory `
        -PassThru

    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 250
        try {
            $health = Invoke-RestMethod -Uri "$baseUri/health" -TimeoutSec 1
        }
        catch {
            $health = $null
        }
    } while (-not $health -and (Get-Date) -lt $deadline)
    Assert-True ($health.status -eq "ok") "MCP health endpoint did not become ready."

    $initialize = @{
        jsonrpc = "2.0"
        id = 1
        method = "initialize"
        params = @{
            protocolVersion = "2025-11-25"
            capabilities = @{}
            clientInfo = @{ name = "CasioEmuMsvc smoke test"; version = "1.0" }
        }
    } | ConvertTo-Json -Depth 10 -Compress
    $response = Invoke-WebRequest -Uri $mcpUri `
        -Method Post `
        -ContentType "application/json" `
        -Headers @{ Accept = "application/json, text/event-stream" } `
        -Body $initialize
    $session = $response.Headers["MCP-Session-Id"]
    Assert-True ([bool]$session) "Initialize response did not contain an MCP session ID."
    $headers = @{
        Accept = "application/json, text/event-stream"
        "MCP-Session-Id" = $session
    }

    function Invoke-McpRequest([int]$Id, [string]$Method, [hashtable]$Params) {
        $body = @{
            jsonrpc = "2.0"
            id = $Id
            method = $Method
            params = $Params
        } | ConvertTo-Json -Depth 20 -Compress
        return Invoke-RestMethod -Uri $mcpUri `
            -Method Post `
            -ContentType "application/json" `
            -Headers $headers `
            -Body $body
    }

    function Invoke-McpTool([int]$Id, [string]$Name, [hashtable]$Arguments = @{}) {
        $response = Invoke-McpRequest $Id "tools/call" @{
            name = $Name
            arguments = $Arguments
        }
        if ($response.error) {
            throw ($response.error | ConvertTo-Json -Depth 10 -Compress)
        }
        if ($response.result.isError) {
            throw ($response.result.content[0].text)
        }
        return $response.result.structuredContent
    }

    $tools = Invoke-McpRequest 2 "tools/list" @{}
    Assert-True ($tools.result.tools.Count -ge 53) "Expected at least 53 MCP tools."

    $status = Invoke-McpTool 3 "get_status"
    Assert-True ($status.model_name -eq "fx-JP900CW") "Unexpected model: $($status.model_name)"
    Assert-True $status.paused "The smoke test model must start paused."

    $registers = Invoke-McpTool 4 "list_registers"
    Assert-True ($registers.registers.Count -gt 0) "No registers were returned."
    $disassembly = Invoke-McpTool 5 "disassemble" @{ address = 0; count = 8 }
    Assert-True ($disassembly.lines.Count -gt 0) "No disassembly was returned."

    $displayBefore = Invoke-McpTool 6 "get_display_settings"
    $null = Invoke-McpTool 7 "set_display_settings" @{
        flashing_threshold = $displayBefore.flashing_threshold
        flashing_brightness = $displayBefore.flashing_brightness
        buffer_select = $displayBefore.buffer_select
        fading_enabled = $displayBefore.fading_enabled
        fading_coefficient = $displayBefore.fading_coefficient
        residual_enabled = $displayBefore.residual_enabled
        residual_alpha_scale = $displayBefore.residual_alpha_scale
        audio_enabled = $displayBefore.audio_enabled
    }

    $null = Invoke-McpTool 8 "add_memory_breakpoint" @{
        address = 0xFFFFF
        write = $false
        break_when_hit = $false
    }
    $hits = Invoke-McpTool 9 "list_memory_breakpoint_hits" @{
        address = 0xFFFFF
        write = $false
    }
    Assert-True ($null -ne $hits.hits) "Memory breakpoint hit list was not returned."
    $null = Invoke-McpTool 10 "clear_memory_breakpoints"

    $snapshot = Invoke-McpTool 11 "save_snapshot" @{ label = "MCP JP900CW smoke test" }
    Assert-True ($snapshot.id -gt 0) "Snapshot save did not return an ID."
    $null = Invoke-McpTool 12 "load_snapshot" @{ id = $snapshot.id }
    $null = Invoke-McpTool 13 "delete_snapshot" @{ id = $snapshot.id }

    $beforeStep = Invoke-McpTool 14 "get_status"
    $null = Invoke-McpTool 15 "step_into"
    Start-Sleep -Milliseconds 500
    $afterStep = Invoke-McpTool 16 "get_status"
    Assert-True $afterStep.paused "The emulator did not pause after step_into."
    Assert-True ($afterStep.program_counter -ne $beforeStep.program_counter) "PC did not change after step_into."

    [pscustomobject]@{
        Result = "PASS"
        Model = $afterStep.model_name
        Tools = $tools.result.tools.Count
        Registers = $registers.registers.Count
        PcBefore = $beforeStep.program_counter
        PcAfter = $afterStep.program_counter
    } | Format-List
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
