<#
.SYNOPSIS
    Stress test for UEAgent server over a persistent TCP connection.

.DESCRIPTION
    Sends $Count commands sequentially over a single persistent TCP connection,
    records response times, and reports statistics. Asserts all commands succeed,
    average response time < 50ms, and max response time < 500ms.
    Exits with code 0 if all assertions pass, 1 otherwise.

.PARAMETER Count
    Number of commands to send. Default: 100.

.PARAMETER Port
    TCP port. Default: 13720.

.PARAMETER Host
    Server hostname or IP. Default: 127.0.0.1.

.PARAMETER Timeout
    Read timeout per command in milliseconds. Default: 10000.

.PARAMETER Command
    Command to use for stress test. Default: get_protocol_info.

.EXAMPLE
    .\Stress-UEAgent.ps1

.EXAMPLE
    .\Stress-UEAgent.ps1 -Count 500 -Command "list_tools"
#>
param(
    [int]$Count      = 100,
    [int]$Port       = 13720,
    [string]$HostAddr = "127.0.0.1",
    [int]$Timeout    = 10000,
    [string]$Command = "get_protocol_info"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Write-Host "Stress Test: $Count commands over persistent TCP connection"
Write-Host "Command: $Command"
Write-Host ([string][char]0x2501 * 30)

$json = @{ command = $Command; params = @{} } | ConvertTo-Json -Depth 5 -Compress

$client = $null
try {
    $client  = [System.Net.Sockets.TcpClient]::new($HostAddr, $Port)
    $stream  = $client.GetStream()

    $successCount = 0
    $failCount    = 0
    $timings      = [System.Collections.Generic.List[double]]::new()

    for ($i = 1; $i -le $Count; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            Send-TcpFrame -Stream $stream -Json $json
            $raw  = Receive-TcpFrame -Stream $stream -TimeoutMs $Timeout
            $resp = $raw | ConvertFrom-Json
            $sw.Stop()

            $elapsedMs = $sw.Elapsed.TotalMilliseconds
            $timings.Add($elapsedMs)

            # Consider success if we got a parseable response (status success or has data)
            $isSuccess = ($null -ne $resp) -and (
                ($resp.PSObject.Properties.Name -contains "status" -and $resp.status -eq "success") -or
                ($resp.PSObject.Properties.Name -contains "data") -or
                ($resp.PSObject.Properties.Name -contains "protocol_version")
            )

            if ($isSuccess) {
                $successCount++
            } else {
                $failCount++
                Write-Warning "Command $i returned non-success: $raw"
            }
        } catch {
            $sw.Stop()
            $failCount++
            Write-Warning "Command $i failed: $_"
            # Attempt to reconnect on failure
            try {
                $client.Close()
                $client = [System.Net.Sockets.TcpClient]::new($HostAddr, $Port)
                $stream = $client.GetStream()
            } catch {
                Write-Error "Reconnect failed: $_"
                break
            }
        }

        # Progress every 10%
        if ($i % [Math]::Max(1, [int]($Count / 10)) -eq 0) {
            Write-Host "  Progress: $i/$Count" -ForegroundColor DarkGray
        }
    }

    # ---------------------------------------------------------------------------
    # Statistics
    # ---------------------------------------------------------------------------

    $sent = $successCount + $failCount

    if ($timings.Count -gt 0) {
        $sorted = $timings | Sort-Object
        $avg    = ($timings | Measure-Object -Average).Average
        $min    = $sorted[0]
        $max    = $sorted[$sorted.Count - 1]

        # P95
        $p95Idx = [Math]::Ceiling($sorted.Count * 0.95) - 1
        if ($p95Idx -lt 0) { $p95Idx = 0 }
        $p95 = $sorted[$p95Idx]
    } else {
        $avg = $min = $max = $p95 = 0
    }

    Write-Host ([string][char]0x2501 * 30)
    Write-Host ("Sent:     {0}" -f $sent)
    Write-Host ("Success:  {0}" -f $successCount)
    Write-Host ("Failed:   {0}" -f $failCount)
    Write-Host ("Avg:      {0:F1}ms" -f $avg)
    Write-Host ("Min:      {0:F1}ms" -f $min)
    Write-Host ("Max:      {0:F1}ms" -f $max)
    Write-Host ("P95:      {0:F1}ms" -f $p95)
    Write-Host ([string][char]0x2501 * 30)

    # ---------------------------------------------------------------------------
    # Assertions
    # ---------------------------------------------------------------------------

    $allPassed = $true

    if ($failCount -eq 0) {
        Write-Host "[PASS] All commands succeeded" -ForegroundColor Green
    } else {
        Write-Host "[FAIL] $failCount command(s) failed" -ForegroundColor Red
        $allPassed = $false
    }

    if ($avg -lt 50) {
        Write-Host ("[PASS] Average response time < 50ms ({0:F1}ms)" -f $avg) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] Average response time >= 50ms ({0:F1}ms)" -f $avg) -ForegroundColor Red
        $allPassed = $false
    }

    if ($max -lt 500) {
        Write-Host ("[PASS] Max response time < 500ms ({0:F1}ms)" -f $max) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] Max response time >= 500ms ({0:F1}ms)" -f $max) -ForegroundColor Red
        $allPassed = $false
    }

    if (-not $allPassed) { exit 1 } else { exit 0 }

} catch {
    Write-Error "Stress test failed: $_"
    exit 1
} finally {
    if ($null -ne $client) { $client.Close() }
}
