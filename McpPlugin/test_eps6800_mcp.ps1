param(
    [string]$ReleaseDirectory = "",
    [string]$Model = "model=.\models\HP300S-plus",
	[int]$Port = 3001,
	[int]$ExpectedRam80 = -1,
	[switch]$CaptureDiagnosticScreens,
	[switch]$CaptureDiagnosticInfo,
	[switch]$InspectLatestSnapshot,
	[switch]$InspectLatestKeyTest,
	[int]$DisassembleAddress = -1,
	[int]$DisassembleCount = 64
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
        -UseBasicParsing `
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

	function Wait-StatusPaused([int]$Id, [int]$TimeoutSeconds = 10) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 200
            $status = Invoke-McpTool $Id "get_status"
        } while (-not $status.paused -and (Get-Date) -lt $deadline)
		return $status
	}

	if ($DisassembleAddress -ge 0) {
		$code = Invoke-McpTool 79 "disassemble" @{
			address = $DisassembleAddress; count = $DisassembleCount
		}
		$code.lines | ForEach-Object { '{0:X5}: {1}' -f $_.address, $_.text }
		return
	}

	if ($InspectLatestKeyTest) {
		$snapshotList = Invoke-McpTool 80 "list_snapshots"
		Assert-True ($snapshotList.snapshots.Count -gt 0) "No saved snapshots were found."
		$snapshot = $snapshotList.snapshots | Sort-Object id | Select-Object -Last 1
		$cases = @(
			@{ Name = "CALC"; Code = 0x52 },
			@{ Name = "M+"; Code = 0x54 }
		)
		$results = @()
		$id = 1000
		foreach ($case in $cases) {
			$null = Invoke-McpTool ($id++) "load_snapshot" @{ id = $snapshot.id }
			$before = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 128 }).bytes
			$beforeStatus = Invoke-McpTool ($id++) "get_status"
			$null = Invoke-McpTool ($id++) "resume"
			Start-Sleep -Milliseconds 250
			$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = $case.Code; pressed = $true }
			Start-Sleep -Milliseconds 150
			$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = $case.Code; pressed = $false }
			Start-Sleep -Milliseconds 600
			$null = Invoke-McpTool ($id++) "pause"
			$afterStatus = Wait-StatusPaused ($id++)
			$after = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 128 }).bytes
			$diff = for ($index = 0; $index -lt 128; ++$index) {
				if ($before[$index] -ne $after[$index]) {
					'{0:X2}:{1:X2}->{2:X2}' -f $index, $before[$index], $after[$index]
				}
			}
			$null = Invoke-McpTool ($id++) "request_screenshot"
			Start-Sleep -Milliseconds 250
			$screen = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
				Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
			$results += [pscustomobject]@{
				Key = $case.Name
				Code = $case.Code
				BeforePc = '0x{0:X}' -f $beforeStatus.program_counter
				AfterPc = '0x{0:X}' -f $afterStatus.program_counter
				Changes = $diff -join ' '
				Screenshot = $screen
			}
		}
		$null = Invoke-McpTool ($id++) "load_snapshot" @{ id = $snapshot.id }
		$null = Invoke-McpTool ($id++) "add_memory_breakpoint" @{
			address = 0x56; write = $true; break_when_hit = $true
		}
		$null = Invoke-McpTool ($id++) "resume"
		Start-Sleep -Milliseconds 250
		$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = 0x54; pressed = $true }
		$acceptedStatus = Wait-StatusPaused ($id++)
		$acceptedCode = Invoke-McpTool ($id++) "disassemble" @{
			address = [Math]::Max(0, $acceptedStatus.program_counter - 12); count = 28
		}
		$acceptedMemory = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 128 }).bytes
		$sequenceResults = @()
		foreach ($sequence in @(
			@{ Name = 'M+ then CALC'; Codes = @(0x54, 0x52) },
			@{ Name = 'CALC then M+'; Codes = @(0x52, 0x54) }
		)) {
			$null = Invoke-McpTool ($id++) "load_snapshot" @{ id = $snapshot.id }
			$null = Invoke-McpTool ($id++) "resume"
			Start-Sleep -Milliseconds 250
			foreach ($code in $sequence.Codes) {
				$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = $code; pressed = $true }
				Start-Sleep -Milliseconds 150
				$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = $code; pressed = $false }
				Start-Sleep -Milliseconds 500
			}
			$null = Invoke-McpTool ($id++) "pause"
			$null = Wait-StatusPaused ($id++)
			$sequenceMemory = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 128 }).bytes
			$sequenceResults += [pscustomobject]@{
				Sequence = $sequence.Name
				Counter56 = '0x{0:X2}' -f $sequenceMemory[0x56]
				LastKey18 = '0x{0:X2}' -f $sequenceMemory[0x18]
			}
		}
		$tableLookups = @()
		foreach ($case in $cases) {
			$null = Invoke-McpTool ($id++) "load_snapshot" @{ id = $snapshot.id }
			$null = Invoke-McpTool ($id++) "add_execution_breakpoint" @{ address = 0x0759 }
			$null = Invoke-McpTool ($id++) "resume"
			Start-Sleep -Milliseconds 250
			$null = Invoke-McpTool ($id++) "keyboard_code" @{ code = $case.Code; pressed = $true }
			$lookupStatus = Wait-StatusPaused ($id++)
			$lookupBefore = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 0x20 }).bytes
			$null = Invoke-McpTool ($id++) "step_into"
			$null = Wait-StatusPaused ($id++)
			$null = Invoke-McpTool ($id++) "step_into"
			$null = Wait-StatusPaused ($id++)
			$lookupAfter = (Invoke-McpTool ($id++) "read_memory" @{ address = 0; size = 0x20 }).bytes
			$tableLookups += [pscustomobject]@{
				Key = $case.Name
				Pc = '0x{0:X}' -f $lookupStatus.program_counter
				ScanIndex18 = '0x{0:X2}' -f $lookupBefore[0x18]
				TableValue16 = '0x{0:X2}' -f $lookupAfter[0x16]
				TablePointer = '{0:X2} {1:X2} {2:X2}' -f $lookupAfter[0x0d], $lookupAfter[0x0c], $lookupAfter[0x0b]
			}
		}
		[pscustomobject]@{
			SnapshotId = $snapshot.id
			SnapshotLabel = $snapshot.label
			AcceptedPc = '0x{0:X}' -f $acceptedStatus.program_counter
			AcceptedCode = ($acceptedCode.lines | ForEach-Object { '{0:X5}: {1}' -f $_.address, $_.text }) -join "`n"
			AcceptedMemory = ($acceptedMemory | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
			Sequences = $sequenceResults
			TableLookups = $tableLookups
			Cases = $results
		} | Format-List
		$results | Format-Table -AutoSize
		$sequenceResults | Format-Table -AutoSize
		$tableLookups | Format-Table -AutoSize
		return
	}

	if ($InspectLatestSnapshot) {
		$snapshotList = Invoke-McpTool 90 "list_snapshots"
		Assert-True ($snapshotList.snapshots.Count -gt 0) "No saved snapshots were found."
		$snapshot = $snapshotList.snapshots | Sort-Object id | Select-Object -Last 1
		$null = Invoke-McpTool 91 "load_snapshot" @{ id = $snapshot.id }
		$before = Invoke-McpTool 92 "get_status"
		$beforeCode = Invoke-McpTool 96 "disassemble" @{ address = [Math]::Max(0, $before.program_counter - 8); count = 16 }
		$diagnosticCode = Invoke-McpTool 99 "disassemble" @{ address = 0x08a0; count = 96 }
		# Snapshots saved by builds with the old Timer0 H/L mapping contain the
		# already-misdecoded internal counter. Rewriting TR0CON rebuilds it from
		# the architectural 26h/27h register bytes using the current implementation.
		$timer0Control = (Invoke-McpTool 97 "read_memory" @{ address = 0x25; size = 1 }).bytes[0]
		$null = Invoke-McpTool 98 "write_memory" @{ address = 0x25; bytes = @($timer0Control) }
		$null = Invoke-McpTool 89 "add_execution_breakpoint" @{ address = 0x08d9 }
		$null = Invoke-McpTool 93 "resume"
		Start-Sleep -Milliseconds 250
		$null = Invoke-McpTool 94 "keyboard_code" @{ code = 0x23; pressed = $true }
		Start-Sleep -Milliseconds 100
		$null = Invoke-McpTool 95 "keyboard_code" @{ code = 0x23; pressed = $false }
		$loopExit = Wait-StatusPaused 88 20
		Assert-True ($loopExit.paused -and $loopExit.program_counter -eq 0x08d9) `
			"Diagnostic ROM checksum loop did not reach its RET instruction."
		$loopChecksum = (Invoke-McpTool 87 "read_memory" @{ address = 0x57; size = 2 }).bytes
		$loopTablePointer = (Invoke-McpTool 86 "read_memory" @{ address = 0x0b; size = 3 }).bytes
		$null = Invoke-McpTool 85 "remove_execution_breakpoint" @{ address = 0x08d9 }
		$null = Invoke-McpTool 84 "resume"

		$observations = @()
		foreach ($delay in @(250, 4000, 8000)) {
			Start-Sleep -Milliseconds $delay
			$status = Invoke-McpTool (100 + $delay) "get_status"
			$code = Invoke-McpTool (150 + $delay) "disassemble" @{
				address = [Math]::Max(0, $status.program_counter - 8); count = 16
			}
			$registerBytes = (Invoke-McpTool (200 + $delay) "read_memory" @{ address = 0x20; size = 27 }).bytes
			$checksumState = (Invoke-McpTool (250 + $delay) "read_memory" @{ address = 0x0a; size = 0x50 }).bytes
			$null = Invoke-McpTool (300 + $delay) "request_screenshot"
			Start-Sleep -Milliseconds 250
			$screen = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
				Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
			$observations += [pscustomobject]@{
				DelayMs = $delay
				Pc = "0x$('{0:X}' -f $status.program_counter)"
				Paused = $status.paused
				Code = ($code.lines | ForEach-Object { '{0:X5}: {1}' -f $_.address, $_.text }) -join "`n"
				Registers20To3A = ($registerBytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
				TablePointer0ATo0D = ($checksumState[0..3] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
				Checksum57To58 = ($checksumState[0x4d..0x4e] | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
				Screenshot = $screen
			}
		}
		$null = Invoke-McpTool 999 "pause"
		[pscustomobject]@{
			SnapshotId = $snapshot.id
			SnapshotLabel = $snapshot.label
			BeforePc = "0x$('{0:X}' -f $before.program_counter)"
			LoopExitChecksum = ($loopChecksum | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
			LoopExitTablePointer = ($loopTablePointer | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
			BeforeCode = ($beforeCode.lines | ForEach-Object { '{0:X5}: {1}' -f $_.address, $_.text }) -join "`n"
			DiagnosticCode = ($diagnosticCode.lines | ForEach-Object { '{0:X5}: {1}' -f $_.address, $_.text }) -join "`n"
			Observations = $observations
		} | Format-List
		$observations | Format-Table -AutoSize
		return
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
    $atBreakpoint = Wait-StatusPaused 33
    Assert-True $atBreakpoint.paused "EPS6800 execution breakpoint did not pause execution."
    Assert-True ($atBreakpoint.program_counter -eq 0x100) "Execution breakpoint stopped at the wrong PC."

    $null = Invoke-McpTool 12 "remove_execution_breakpoint" @{ address = 0x100 }
    $beforeStep = Invoke-McpTool 13 "get_status"
    $null = Invoke-McpTool 14 "step_into"
    $afterStep = Wait-StatusPaused 34
    Assert-True $afterStep.paused "EPS6800 step_into did not pause."
    Assert-True ($afterStep.program_counter -ne $beforeStep.program_counter) "EPS6800 step_into did not execute one instruction."

    $null = Invoke-McpTool 16 "reset"
    $null = Invoke-McpTool 17 "step_over"
    $afterStepOver = Wait-StatusPaused 35
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
	$recordingBaseline = Invoke-McpTool 36 "get_status"
	$null = Invoke-McpTool 29 "resume"
	$recordingDeadline = (Get-Date).AddSeconds(10)
	do {
		Start-Sleep -Milliseconds 200
		$recordingStatus = Invoke-McpTool 37 "get_status"
	} while (-not $recordingStatus.paused -and $recordingStatus.program_counter -eq $recordingBaseline.program_counter -and (Get-Date) -lt $recordingDeadline)
	$null = Invoke-McpTool 30 "pause"
	$null = Invoke-McpTool 31 "stop_call_recording"
	$calls = Invoke-McpTool 32 "list_function_calls"
	Assert-True ($calls.calls.Count -gt 0) "EPS6800 Call Analysis did not receive call hooks."

	# HP300S+ reserves model button codes FE/FF for RESET and independent ON.
	# Verify the model parser and desktop Keyboard path, not only the core API.
	$null = Invoke-McpTool 61 "step_into"
	$beforeResetButton = Wait-StatusPaused 62
	Assert-True ($beforeResetButton.program_counter -ne 0) "Precondition for RESET button test was not reached."
	$null = Invoke-McpTool 63 "keyboard_code" @{ code = 0xFE; pressed = $true }
	$afterResetButton = Invoke-McpTool 64 "get_status"
	Assert-True ($afterResetButton.program_counter -eq 0) "HP logo RESET button did not return EPS6800 to the reset vector."
	$null = Invoke-McpTool 65 "keyboard_code" @{ code = 0xFE; pressed = $false }

	$null = Invoke-McpTool 66 "add_execution_breakpoint" @{ address = 0x1D8 }
	$null = Invoke-McpTool 67 "resume"
	Start-Sleep -Milliseconds 500
	$null = Invoke-McpTool 68 "keyboard_code" @{ code = 0x56; pressed = $true } # SHIFT
	$null = Invoke-McpTool 69 "keyboard_code" @{ code = 0x03; pressed = $true } # 7
	Start-Sleep -Milliseconds 100
	$null = Invoke-McpTool 70 "keyboard_code" @{ code = 0xFF; pressed = $true } # ON
	$diagnosticEntry = Wait-StatusPaused 71
	Assert-True $diagnosticEntry.paused "HP300S+ diagnostic entry breakpoint was not reached."
	Assert-True ($diagnosticEntry.program_counter -eq 0x1D8) `
		"SHIFT+7+ON stopped at the wrong PC: $($diagnosticEntry.program_counter)"
	$null = Invoke-McpTool 72 "keyboard_code" @{ code = 0xFF; pressed = $false }
	$null = Invoke-McpTool 73 "keyboard_code" @{ code = 0x03; pressed = $false }
	$null = Invoke-McpTool 74 "keyboard_code" @{ code = 0x56; pressed = $false }
	$null = Invoke-McpTool 75 "remove_execution_breakpoint" @{ address = 0x1D8 }
	$diagnosticScreenshot = ""
	$diagnosticAcScreenshot = ""
	$diagnostic9Screenshot = ""
	$diagnosticScanFlag = -1
	$diagnosticKeyboardRegisters = @()
	if ($CaptureDiagnosticScreens -or $CaptureDiagnosticInfo) {
		$null = Invoke-McpTool 76 "resume"
		Start-Sleep -Milliseconds 750
		$null = Invoke-McpTool 77 "request_screenshot"
		Start-Sleep -Milliseconds 500
		$diagnosticScreenshot = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
			Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
	}
	if ($CaptureDiagnosticInfo) {
		$null = Invoke-McpTool 78 "keyboard_code" @{ code = 0x23; pressed = $true } # 9
		Start-Sleep -Milliseconds 100
		$null = Invoke-McpTool 79 "keyboard_code" @{ code = 0x23; pressed = $false }
		Start-Sleep -Seconds 15
		$null = Invoke-McpTool 80 "request_screenshot"
		Start-Sleep -Milliseconds 500
		$diagnosticAcScreenshot = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
			Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
		$diagnosticScanFlag = (Invoke-McpTool 82 "read_memory" @{ address = 0x40; size = 1 }).bytes[0]
		$diagnosticKeyboardRegisters = (Invoke-McpTool 87 "read_memory" @{ address = 0x20; size = 27 }).bytes
		$null = Invoke-McpTool 83 "keyboard_code" @{ code = 0x23; pressed = $true } # 9 again
		Start-Sleep -Milliseconds 100
		$null = Invoke-McpTool 84 "keyboard_code" @{ code = 0x23; pressed = $false }
		Start-Sleep -Milliseconds 750
		$null = Invoke-McpTool 85 "request_screenshot"
		Start-Sleep -Milliseconds 500
		$diagnostic9Screenshot = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
			Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
		$null = Invoke-McpTool 86 "pause"
	}
	elseif ($CaptureDiagnosticScreens) {
		$null = Invoke-McpTool 78 "keyboard_code" @{ code = 0x43; pressed = $true } # AC
		Start-Sleep -Milliseconds 100
		$null = Invoke-McpTool 79 "keyboard_code" @{ code = 0x43; pressed = $false }
		Start-Sleep -Milliseconds 750
		$null = Invoke-McpTool 80 "request_screenshot"
		Start-Sleep -Milliseconds 500
		$diagnosticAcScreenshot = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
			Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
		$diagnosticScanFlag = (Invoke-McpTool 82 "read_memory" @{ address = 0x40; size = 1 }).bytes[0]
		$null = Invoke-McpTool 83 "keyboard_code" @{ code = 0x23; pressed = $true } # 9
		Start-Sleep -Milliseconds 100
		$null = Invoke-McpTool 84 "keyboard_code" @{ code = 0x23; pressed = $false }
		Start-Sleep -Milliseconds 750
		$null = Invoke-McpTool 85 "request_screenshot"
		Start-Sleep -Milliseconds 500
		$diagnostic9Screenshot = (Get-ChildItem -LiteralPath $ReleaseDirectory -Filter "screenshot-*.png" |
			Sort-Object LastWriteTimeUtc | Select-Object -Last 1).FullName
		Assert-True ([bool]$diagnosticScreenshot -and (Test-Path -LiteralPath $diagnosticScreenshot)) `
			"DIAGNOSTIC screenshot was not created."
		Assert-True ([bool]$diagnosticAcScreenshot -and (Test-Path -LiteralPath $diagnosticAcScreenshot)) `
			"Post-AC diagnostic screenshot was not created."
		Assert-True ([bool]$diagnostic9Screenshot -and (Test-Path -LiteralPath $diagnostic9Screenshot)) `
			"Post-9 diagnostic screenshot was not created."
		$diagnosticHash = (Get-FileHash -LiteralPath $diagnosticScreenshot -Algorithm SHA256).Hash
		$diagnosticAcHash = (Get-FileHash -LiteralPath $diagnosticAcScreenshot -Algorithm SHA256).Hash
		$diagnostic9Hash = (Get-FileHash -LiteralPath $diagnostic9Screenshot -Algorithm SHA256).Hash
		Assert-True ($diagnosticHash -ne $diagnosticAcHash) `
			"Pressing AC did not advance the initial DIAGNOSTIC display."
		$null = Invoke-McpTool 86 "pause"
	}

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
		ResetButton = "PASS"
		DiagnosticEntry = "PASS (PC=0x$('{0:X}' -f $diagnosticEntry.program_counter))"
		DiagnosticScreenshot = $diagnosticScreenshot
		DiagnosticAcScreenshot = $diagnosticAcScreenshot
		Diagnostic9Screenshot = $diagnostic9Screenshot
		DiagnosticScanFlag40 = "0x$('{0:X2}' -f $diagnosticScanFlag)"
		DiagnosticKeyboardRegisters = ($diagnosticKeyboardRegisters | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
		Diagnostic9Changed = $diagnosticAcHash -ne $diagnostic9Hash
    } | Format-List
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
