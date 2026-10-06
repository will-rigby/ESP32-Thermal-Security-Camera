param(
    [Parameter(Mandatory=$true)][string]$BaseUrl,
    [ValidateRange(1,300)][int]$Seconds=5,
    [ValidateRange(0.1,20)][double]$MinimumFps=18
)
$ErrorActionPreference='Stop'
$uri=[UriBuilder]::new($BaseUrl)
$uri.Scheme=if($uri.Scheme -eq 'https'){'wss'}else{'ws'}
$uri.Path='/stream.yuy2';$uri.Query=''
$socket=[Net.WebSockets.ClientWebSocket]::new()
$cancel=[Threading.CancellationTokenSource]::new(5000)
try { $socket.ConnectAsync($uri.Uri,$cancel.Token).GetAwaiter().GetResult() | Out-Null } catch { $socket.Dispose();throw } finally { $cancel.Dispose() }
$buffer=[byte[]]::new(9985)
$request=[byte[]]@(1)
$arrivals=[Collections.Generic.List[double]]::new()
$lastSerial=-1L
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
    while($watch.Elapsed.TotalSeconds -lt $Seconds) {
        $started=$watch.Elapsed.TotalMilliseconds
        $cancel=[Threading.CancellationTokenSource]::new(2000)
        try {
            $socket.SendAsync([ArraySegment[byte]]::new($request),[Net.WebSockets.WebSocketMessageType]::Binary,$true,$cancel.Token).GetAwaiter().GetResult() | Out-Null
            $count=0
            do {
                if($count -ge $buffer.Length){throw 'Oversized video response'}
                $part=$socket.ReceiveAsync([ArraySegment[byte]]::new($buffer,$count,$buffer.Length-$count),$cancel.Token).GetAwaiter().GetResult()
                if($part.MessageType -ne [Net.WebSockets.WebSocketMessageType]::Binary){throw 'Video socket closed or returned nonbinary data'}
                $count+=$part.Count
            } while(-not $part.EndOfMessage)
        } finally { $cancel.Dispose() }
        if($count) {
            if($count -ne 9984 -or [Text.Encoding]::ASCII.GetString($buffer,0,4) -ne 'YUY2' -or
               [BitConverter]::ToUInt16($buffer,4) -ne 1 -or [BitConverter]::ToUInt16($buffer,6) -ne 64 -or
               [BitConverter]::ToUInt16($buffer,8) -ne 80 -or [BitConverter]::ToUInt16($buffer,10) -ne 62) { throw 'Invalid YUY2 frame header' }
            $serial=[BitConverter]::ToUInt32($buffer,12)
            if($serial -eq $lastSerial){throw 'Duplicate frame returned as fresh'}
            $lastSerial=$serial
            $arrivals.Add($watch.Elapsed.TotalSeconds)
            for($offset=64;$offset -lt 9984;$offset+=2) {
                if($buffer[$offset] -lt 16 -or $buffer[$offset] -gt 235 -or $buffer[$offset+1] -ne 128){throw 'Invalid grayscale YUY2 pixel'}
            }
        }
        $wait=[int][Math]::Ceiling(50-($watch.Elapsed.TotalMilliseconds-$started))
        # Windows' coarse sleep timer otherwise turns 50 ms into ~63 ms and
        # falsely caps a healthy 20 FPS camera at ~16 FPS. Spin only the tail.
        if($wait -gt 16){[Threading.Thread]::Sleep($wait-16)}
        while($watch.Elapsed.TotalMilliseconds -lt $started+50){[Threading.Thread]::SpinWait(100)}
    }
} finally { $socket.Abort();$socket.Dispose();$watch.Stop() }
if($arrivals.Count -lt 2){throw 'Fewer than two fresh frames received'}
$fps=($arrivals.Count-1)/($arrivals[$arrivals.Count-1]-$arrivals[0])
[pscustomobject]@{Frames=$arrivals.Count;Seconds=[Math]::Round($watch.Elapsed.TotalSeconds,3);MeasuredFPS=[Math]::Round($fps,2);Format='YUY2';Width=80;Height=62;FrameBytes=9920}
if($fps -lt $MinimumFps -or $arrivals.Count -lt [Math]::Floor($Seconds*$MinimumFps)){throw "Video delivered fewer than $MinimumFps FPS"}
Write-Output 'PASS: raw WebSocket frames and host arrival rate; physical movement-to-screen latency still needs measurement'
