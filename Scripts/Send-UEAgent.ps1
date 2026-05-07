<#
.SYNOPSIS
    Send a single command to UEAgent server via TCP or HTTP.

.DESCRIPTION
    Connects to the UEAgent server and sends a JSON command using either TCP (with
    4-byte LE uint32 length-prefix framing) or HTTP REST API.

.PARAMETER Command
    The command name to send (e.g. "list_tools", "get_protocol_info").

.PARAMETER Params
    JSON string of parameters. Defaults to "{}".

.PARAMETER Port
    TCP port. Default: 13720.

.PARAMETER Host
    Server hostname or IP. Default: 127.0.0.1.

.PARAMETER Http
    Use HTTP mode instead of TCP.

.PARAMETER HttpPort
    HTTP port. Default: 13721.

.PARAMETER Timeout
    Read timeout in milliseconds. Default: 10000.

.EXAMPLE
    .\Send-UEAgent.ps1 -Command "list_tools"

.EXAMPLE
    .\Send-UEAgent.ps1 -Command "spawn_actor" -Params '{"class":"StaticMeshActor","location":{"x":0,"y":0,"z":0}}'

.EXAMPLE
    .\Send-UEAgent.ps1 -Command "get_protocol_info" -Http
#>
param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Command,

    [Parameter(Position=1)]
    [string]$Params = "{}",

    [int]$Port = 13720,
    [string]$HostAddr = "127.0.0.1",
    [switch]$Http,
    [int]$HttpPort = 13721,
    [int]$Timeout = 10000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Send-TcpFrame {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [string]$Json
    )
    $bytes   = [System.Text.Encoding]::UTF8.GetBytes($Json)
    $lenBytes = [BitConverter]::GetBytes([uint32]$bytes.Length)
    $Stream.Write($lenBytes, 0, 4)
    $Stream.Write($bytes, 0, $bytes.Length)
    $Stream.Flush()
}

function Receive-TcpFrame {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [int]$TimeoutMs = 10000
    )
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

function Send-HttpCommand {
    param(
        [string]$HostAddr,
        [int]$HttpPort,
        [string]$Command,
        [string]$Params = "{}"
    )
    $uri     = "http://${HostAddr}:${HttpPort}/api/v1/execute"
    $paramsObj = $Params | ConvertFrom-Json
    $body    = @{ command = $Command; params = $paramsObj } | ConvertTo-Json -Depth 10 -Compress
    $response = Invoke-RestMethod -Uri $uri -Method Post -Body $body -ContentType "application/json"
    return $response
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Validate params JSON
try {
    $null = $Params | ConvertFrom-Json
} catch {
    Write-Error "Invalid JSON in -Params: $_"
    exit 1
}

if ($Http) {
    # HTTP mode
    try {
        $result = Send-HttpCommand -HostAddr $HostAddr -HttpPort $HttpPort -Command $Command -Params $Params
        $result | ConvertTo-Json -Depth 20
    } catch {
        Write-Error "HTTP request failed: $_"
        exit 1
    }
} else {
    # TCP mode
    $client = $null
    try {
        $client = [System.Net.Sockets.TcpClient]::new($HostAddr, $Port)
        $stream = $client.GetStream()

        $paramsObj = $Params | ConvertFrom-Json
        $json = @{ command = $Command; params = $paramsObj } | ConvertTo-Json -Depth 10 -Compress

        Send-TcpFrame -Stream $stream -Json $json
        $responseJson = Receive-TcpFrame -Stream $stream -TimeoutMs $Timeout

        $responseJson | ConvertFrom-Json | ConvertTo-Json -Depth 20
    } catch {
        Write-Error "TCP command failed: $_"
        exit 1
    } finally {
        if ($null -ne $client) { $client.Close() }
    }
}
