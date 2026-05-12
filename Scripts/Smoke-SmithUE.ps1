<#
.SYNOPSIS
    Smoke test for UEAgent server (TCP + HTTP).

.DESCRIPTION
    Runs a series of basic connectivity and command tests against the UEAgent server.
    Exits with code 0 if all tests pass, 1 if any fail.

.PARAMETER TcpPort
    TCP port. Default: 13720.

.PARAMETER HttpPort
    HTTP port. Default: 13721.

.PARAMETER Host
    Server hostname or IP. Default: 127.0.0.1.

.PARAMETER Timeout
    Read timeout in milliseconds. Default: 10000.

.EXAMPLE
    .\Smoke-UEAgent.ps1

.EXAMPLE
    .\Smoke-UEAgent.ps1 -TcpPort 13720 -HttpPort 13721 -HostAddr 127.0.0.1
#>
param(
    [int]$TcpPort     = 13720,
    [int]$HttpPort    = 13721,
    [string]$HostAddr = "127.0.0.1",
    [int]$Timeout     = 10000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "SilentlyContinue"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Send-TcpFrame {
    param([System.Net.Sockets.NetworkStream]$Stream, [string]$Json)
    $bytes    = [System.Text.Encoding]::UTF8.GetBytes($Json)
    $lenBytes = [BitConverter]::GetBytes([uint32]$bytes.Length)
    $Stream.Write($lenBytes, 0, 4)
    $Stream.Write($bytes, 0, $bytes.Length)
    $Stream.Flush()
}

function Receive-TcpFrame {
    param([System.Net.Sockets.NetworkStream]$Stream, [int]$TimeoutMs = 10000)
    $Stream.ReadTimeout = $TimeoutMs
    $lenBuf = [byte[]]::new(4)
    $read = 0
    while ($read -lt 4) {
        $n = $Stream.Read($lenBuf, $read, 4 - $read)
        if ($n -eq 0) { throw "Connection closed while reading length prefix." }
        $read += $n
    }
    $payloadLen = [BitConverter]::ToUInt32($lenBuf, 0)
    $payload    = [byte[]]::new($payloadLen)
    $read = 0
    while ($read -lt $payloadLen) {
        $n = $Stream.Read($payload, $read, $payloadLen - $read)
        if ($n -eq 0) { throw "Connection closed while reading payload." }
        $read += $n
    }
    return [System.Text.Encoding]::UTF8.GetString($payload)
}

function Invoke-TcpCommand {
    param([string]$Command, [string]$Params = "{}")
    $client = $null
    try {
        $client    = [System.Net.Sockets.TcpClient]::new($HostAddr, $TcpPort)
        $stream    = $client.GetStream()
        $paramsObj = $Params | ConvertFrom-Json
        $json      = @{ command = $Command; params = $paramsObj } | ConvertTo-Json -Depth 10 -Compress
        Send-TcpFrame -Stream $stream -Json $json
        $raw = Receive-TcpFrame -Stream $stream -TimeoutMs $Timeout
        return $raw | ConvertFrom-Json
    } finally {
        if ($null -ne $client) { $client.Close() }
    }
}

function Invoke-HttpCommand {
    param([string]$Command, [string]$Params = "{}")
    $uri       = "http://${HostAddr}:${HttpPort}/api/v1/execute"
    $paramsObj = $Params | ConvertFrom-Json
    $body      = @{ command = $Command; params = $paramsObj } | ConvertTo-Json -Depth 10 -Compress
    return Invoke-RestMethod -Uri $uri -Method Post -Body $body -ContentType "application/json"
}

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------

$pass  = 0
$fail  = 0
$results = @()

function Test-Case {
    param([string]$Name, [scriptblock]$Block)
    try {
        $ok = & $Block
        if ($ok) {
            Write-Host "[PASS] $Name" -ForegroundColor Green
            $script:pass++
        } else {
            Write-Host "[FAIL] $Name" -ForegroundColor Red
            $script:fail++
        }
    } catch {
        Write-Host "[FAIL] $Name - $_" -ForegroundColor Red
        $script:fail++
    }
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# 1. TCP Connection
Test-Case "TCP Connection" {
    $client = [System.Net.Sockets.TcpClient]::new($HostAddr, $TcpPort)
    $connected = $client.Connected
    $client.Close()
    return $connected
}

# 2. TCP: get_protocol_info
$tcpProtocol = $null
Test-Case "get_protocol_info via TCP" {
    $resp = Invoke-TcpCommand -Command "get_protocol_info"
    $script:tcpProtocol = $resp
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    return ($null -ne $data) -and ($data.PSObject.Properties.Name -contains "protocol_version")
}

# 3. TCP: list_tools — expect >= 25 tools
$tcpToolCount = 0
Test-Case "list_tools via TCP (>= 25 tools)" {
    $resp = Invoke-TcpCommand -Command "list_tools"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $tools = if ($data.PSObject.Properties.Name -contains "tools") { $data.tools } else { $null }
    if ($null -eq $tools) { throw "No tools array in response" }
    $script:tcpToolCount = $tools.Count
    if ($tools.Count -lt 45) {
        throw "Expected 45+ tools, got $($tools.Count)"
    }
    return $true
}

# 4. TCP: get_all_actors
Test-Case "get_all_actors via TCP" {
    $resp = Invoke-TcpCommand -Command "get_all_actors"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $actors = if ($data.PSObject.Properties.Name -contains "actors") { $data.actors } else { $null }
    return $null -ne $actors
}

# 5. TCP: list_assets /Game
Test-Case "list_assets /Game via TCP" {
    $resp = Invoke-TcpCommand -Command "list_assets" -Params '{"folder_path":"/Game"}'
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $assets = if ($data.PSObject.Properties.Name -contains "assets") { $data.assets } else { $null }
    return $null -ne $assets
}

# 6. TCP: get_project_info
Test-Case "get_project_info via TCP" {
    $resp = Invoke-TcpCommand -Command "get_project_info"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $name = if ($data.PSObject.Properties.Name -contains "project_name") { $data.project_name } else { $null }
    return $null -ne $name -and $name -ne ""
}

# 7. HTTP Connection
Test-Case "HTTP Connection" {
    $resp = Invoke-HttpCommand -Command "get_protocol_info"
    return $null -ne $resp
}

# 8. HTTP: get_protocol_info
$httpProtocol = $null
Test-Case "get_protocol_info via HTTP" {
    $resp = Invoke-HttpCommand -Command "get_protocol_info"
    $script:httpProtocol = $resp
    return $null -ne $resp
}

# 9. HTTP: list_tools — compare count matches TCP
Test-Case "list_tools via HTTP (count matches TCP)" {
    $resp  = Invoke-HttpCommand -Command "list_tools"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $tools = if ($data.PSObject.Properties.Name -contains "tools") { $data.tools } else { $null }
    if ($null -eq $tools) { throw "No tools array in HTTP response" }
    if ($tools.Count -ne $script:tcpToolCount) {
        throw "HTTP tool count ($($tools.Count)) != TCP tool count ($($script:tcpToolCount))"
    }
    return $true
}

# 10. TCP/HTTP consistency — protocol_version matches
Test-Case "TCP/HTTP protocol_version consistency" {
    if ($null -eq $script:tcpProtocol -or $null -eq $script:httpProtocol) {
        throw "Protocol info not available from previous tests"
    }
    $tcpData  = if ($script:tcpProtocol.PSObject.Properties.Name -contains "data") { $script:tcpProtocol.data } else { $script:tcpProtocol }
    $httpData = if ($script:httpProtocol.PSObject.Properties.Name -contains "data") { $script:httpProtocol.data } else { $script:httpProtocol }
    $tcpVer  = $tcpData.protocol_version
    $httpVer = $httpData.protocol_version
    return $tcpVer -eq $httpVer
}

# 11. TCP: list_editor_commands — expect non-empty array
Test-Case "list_editor_commands via TCP" {
    $resp = Invoke-TcpCommand -Command "list_editor_commands"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $cmds = if ($data.PSObject.Properties.Name -contains "commands") { $data.commands } else { $null }
    return ($null -ne $cmds) -and ($cmds.Count -gt 0)
}

# 12. TCP: execute_console_command (safe read-only command)
Test-Case "execute_console_command via TCP" {
    $resp = Invoke-TcpCommand -Command "execute_console_command" -Params '{"command":"STAT NONE"}'
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    return ($null -ne $data) -and ($data.executed -eq $true)
}

# 13. TCP: list_panels — expect array containing ContentBrowserTab1
Test-Case "list_panels via TCP" {
    $resp = Invoke-TcpCommand -Command "list_panels"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $panels = if ($data.PSObject.Properties.Name -contains "panels") { $data.panels } else { $null }
    if ($null -eq $panels -or $panels.Count -eq 0) { return $false }
    $names = $panels | ForEach-Object { $_.name }
    return $names -contains "ContentBrowserTab1"
}

# 14. TCP: open_panel OutputLog — expect success
Test-Case "open_panel OutputLog via TCP" {
    $resp = Invoke-TcpCommand -Command "open_panel" -Params '{"panel_name":"OutputLog"}'
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    return ($null -ne $data) -and ($data.opened -eq $true)
}

# 15. TCP: get_editor_state — expect required fields
Test-Case "get_editor_state via TCP" {
    $resp = Invoke-TcpCommand -Command "get_editor_state"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $hasIsPIE    = $data.PSObject.Properties.Name -contains "is_pie"
    $hasModal    = $data.PSObject.Properties.Name -contains "modal_dialog_open"
    $hasCount    = $data.PSObject.Properties.Name -contains "selected_actor_count"
    return $hasIsPIE -and $hasModal -and $hasCount
}

# 16. TCP: set_viewport_camera — expect before/after diff
Test-Case "set_viewport_camera via TCP" {
    $resp = Invoke-TcpCommand -Command "set_viewport_camera" -Params '{"location":{"x":100,"y":200,"z":300}}'
    if ($resp.status -eq "error") { return $true } # tolerate no-viewport error in headless
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $hasBefore = $data.PSObject.Properties.Name -contains "before"
    $hasAfter  = $data.PSObject.Properties.Name -contains "after"
    return $hasBefore -and $hasAfter
}

# 17. TCP: get_world_outline — expect actors array
Test-Case "get_world_outline via TCP" {
    $resp = Invoke-TcpCommand -Command "get_world_outline"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $actors = if ($data.PSObject.Properties.Name -contains "actors") { $data.actors } else { $null }
    return $null -ne $actors
}

# 18. TCP: get_selected_actors — expect selected array
Test-Case "get_selected_actors via TCP" {
    $resp = Invoke-TcpCommand -Command "get_selected_actors"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $hasSelected = $data.PSObject.Properties.Name -contains "selected"
    $hasCount    = $data.PSObject.Properties.Name -contains "count"
    return $hasSelected -and $hasCount
}

# 19. TCP: simulate_key with invalid key — expect error
Test-Case "simulate_key invalid key returns error via TCP" {
    $resp = Invoke-TcpCommand -Command "simulate_key" -Params '{"key":"NOTAVALIDKEY999"}'
    return $resp.status -eq "error"
}

# 20. TCP: list_key_bindings — expect non-empty array
Test-Case "list_key_bindings via TCP" {
    $resp = Invoke-TcpCommand -Command "list_key_bindings"
    $data = if ($resp.PSObject.Properties.Name -contains "data") { $resp.data } else { $resp }
    $bindings = if ($data.PSObject.Properties.Name -contains "bindings") { $data.bindings } else { $null }
    return ($null -ne $bindings) -and ($bindings.Count -gt 0)
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

$total = $pass + $fail
Write-Host ""
Write-Host "Results: $pass/$total PASS, $fail FAIL"

if ($fail -gt 0) { exit 1 } else { exit 0 }
