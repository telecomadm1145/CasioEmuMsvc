param(
    [string]$ReleaseDirectory = "",
    [string]$Model = "model=.\models\HP300S-plus",
    [int]$Port = 3001,
	[int]$ExpectedRam80 = -1
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
        -WindowStyle Hidden `
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
            clientInfo = @{ name = "EPS6800 debugger smoke test"; version = "1.0" }
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

    function Invoke-McpTool([int]$Id, [string]$Name, [hashtable]$Arguments = @{}) {
        $body = @{
            jsonrpc = "2.0"
            id = $Id
            method = "tools/call"
            params = @{ name = $Name; arguments = $Arguments }
        } | ConvertTo-Json -Depth 20 -Compress
        $reply = Invoke-RestMethod -Uri $mcpUri `
            -Method Post `
            -ContentType "application/json" `
            -Headers $headers `
            -Body $body
        if ($reply.error) {
            throw ($reply.error | ConvertTo-Json -Depth 10 -Compress)
        }
        if ($reply.result.isError) {
            throw $reply.result.content[0].text
        }
        return $reply.result.structuredContent
    }

    $initial = Invoke-McpTool 2 "get_status"
    Assert-True $initial.paused "EPS6800 model must start paused."
    Assert-True ($initial.program_counter -eq 0) "Unexpected reset PC: $($initial.program_counter)"

    $registers = Invoke-McpTool 3 "list_registers"
    Assert-True ($registers.registers.Count -eq 129) "Expected PC plus 128 EPS6800 registers."
    $acc = Invoke-McpTool 4 "read_register" @{ name = "acc" }
    $statusRegister = Invoke-McpTool 5 "read_register" @{ name = "status" }
    $statusMemory = Invoke-McpTool 6 "read_memory" @{ address = 0x0F; size = 1 }
    Assert-True ($statusMemory.bytes[0] -eq $statusRegister.value) "STATUS SFR and memory view disagree."
	if ($ExpectedRam80 -ge 0) {
		$persistedRam = Invoke-McpTool 60 "read_memory" @{ address = 0x80; size = 1 }
		Assert-True ($persistedRam.bytes[0] -eq $ExpectedRam80) `
			"EPS6800 persisted RAM was not restored (got $($persistedRam.bytes[0]))."
	}

    $code = Invoke-McpTool 7 "read_code" @{ address = 0; count = 2 }
    Assert-True ($code.words[0] -eq 0x0020) "EPS ROM word decoding is incorrect at reset."

    $null = Invoke-McpTool 8 "add_execution_breakpoint" @{ address = 0x100 }
    $listed = Invoke-McpTool 9 "list_execution_breakpoints"
    Assert-True ($listed.breakpoints -contains 0x100) "Execution breakpoint was not registered."
    $null = Invoke-McpTool 10 "resume"
    Start-Sleep -Milliseconds 500
    $atBreakpoint = Invoke-McpTool 11 "get_status"
    Assert-True $atBreakpoint.paused "EPS6800 execution breakpoint did not pause execution."
    Assert-True ($atBreakpoint.program_counter -eq 0x100) "Execution breakpoint stopped at the wrong PC."

    $null = Invoke-McpTool 12 "remove_execution_breakpoint" @{ address = 0x100 }
    $beforeStep = Invoke-McpTool 13 "get_status"
    $null = Invoke-McpTool 14 "step_into"
    Start-Sleep -Milliseconds 250
    $afterStep = Invoke-McpTool 15 "get_status"
    Assert-True $afterStep.paused "EPS6800 step_into did not pause."
    Assert-True ($afterStep.program_counter -ne $beforeStep.program_counter) "EPS6800 step_into did not execute one instruction."

    $null = Invoke-McpTool 16 "reset"
    $null = Invoke-McpTool 17 "step_over"
    Start-Sleep -Milliseconds 250
    $afterStepOver = Invoke-McpTool 18 "get_status"
    Assert-True $afterStepOver.paused "EPS6800 step_over did not pause."
    Assert-True ($afterStepOver.program_counter -eq 0x100) "Non-CALL step_over did not execute exactly one LJMP instruction."

    $null = Invoke-McpTool 19 "add_memory_breakpoint" @{
        address = 0x80
        write = $true
        break_when_hit = $true
        compare_data = $true
        data = 0x50
        mask = 0xF0
        skip_count = 2
    }
    $memoryBreakpoints = Invoke-McpTool 20 "list_memory_breakpoints"
    $memoryBreakpoint = @($memoryBreakpoints.breakpoints | Where-Object { $_.address -eq 0x80 -and $_.write })[0]
    Assert-True ($null -ne $memoryBreakpoint) "EPS6800 conditional memory breakpoint was not registered."
    Assert-True ($memoryBreakpoint.compare_data -and $memoryBreakpoint.data -eq 0x50 `
        -and $memoryBreakpoint.mask -eq 0xF0 -and $memoryBreakpoint.skip_count -eq 2) `
        "EPS6800 memory breakpoint Data/Mask/Counter settings were not preserved."
    $null = Invoke-McpTool 21 "clear_memory_breakpoints"

	$backtrace = Invoke-McpTool 22 "get_backtrace"
	Assert-True ($backtrace.backtrace -like "PC=*") "EPS6800 text backtrace is empty."
	$null = Invoke-McpTool 23 "set_address_lock" @{ address = 0x80; value = 0xA5; locked = $true }
	$locks = Invoke-McpTool 24 "list_address_locks"
	$lock = @($locks.addresses | Where-Object { $_.address -eq 0x80 })[0]
	Assert-True ($null -ne $lock -and $lock.locked -and $lock.value -eq 0xA5) `
		"EPS6800 address lock was not retained by the debugger."
	$null = Invoke-McpTool 25 "clear_address_locks"
	$reload = Invoke-McpTool 26 "hot_reload_rom"
	Assert-True $reload.success "EPS6800 hot ROM reload failed."
	$afterReload = Invoke-McpTool 27 "get_status"
	Assert-True ($afterReload.program_counter -eq 0) "EPS6800 hot reload did not reset the reloaded core."
	$null = Invoke-McpTool 28 "start_call_recording"
	$null = Invoke-McpTool 29 "resume"
	Start-Sleep -Milliseconds 300
	$null = Invoke-McpTool 30 "pause"
	$null = Invoke-McpTool 31 "stop_call_recording"
	$calls = Invoke-McpTool 32 "list_function_calls"
	Assert-True ($calls.calls.Count -gt 0) "EPS6800 Call Analysis did not receive call hooks."

    [pscustomobject]@{
        Result = "PASS"
        Model = $initial.model_name
        Registers = $registers.registers.Count
        Acc = $acc.value
        Status = $statusRegister.value
        BreakpointPc = $atBreakpoint.program_counter
        StepPc = $afterStep.program_counter
        StepOverPc = $afterStepOver.program_counter
        ConditionalMemoryBreakpoint = "PASS"
		Backtrace = "PASS"
		AddressLock = "PASS"
		HotReload = "PASS"
		CallAnalysis = "PASS ($($calls.calls.Count) calls)"
    } | Format-List
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
