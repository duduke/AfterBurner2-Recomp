#Requires -RunAsAdministrator
<#
Record CPU scheduling/stacks and optional GPU events for a short game run. This does
not edit Windows profiling/security policy or launch the game elevated.
Run the ordinary S-video launcher separately while the capture is active.
#>
param([ValidateRange(15,120)][int]$Seconds=90,[switch]$IncludeGpu)
$ErrorActionPreference='Stop'
$ab2Wpr=Join-Path $env:SystemRoot 'System32/wpr.exe'
$ab2Status=(& $ab2Wpr -status 2>&1 | Out-String)
if($LASTEXITCODE -ne 0 -or $ab2Status -notmatch 'WPR is not recording'){
    throw "Capture was not started. Inspect 'wpr -status' and preserve any existing recording.`n$ab2Status"
}
$ab2Root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$ab2Capture=Join-Path $ab2Root ('build/oracle/windows-cpu-stalls/'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[void](New-Item -ItemType Directory -Path $ab2Capture)
$ab2Trace=Join-Path $ab2Capture $(if($IncludeGpu){'cpu-gpu.etl'}else{'cpu.etl'})
$ab2Profiles=@('CPU')
if($IncludeGpu){$ab2Profiles+='GPU'}
$ab2StartArgs=@()
foreach($ab2Profile in $ab2Profiles){$ab2StartArgs+=@('-start',$ab2Profile)}
$ab2StartArgs+='-filemode'
$ab2Started=$false
try{
    & $ab2Wpr @ab2StartArgs
    if($LASTEXITCODE -ne 0){throw "Windows profiling is unavailable (exit $LASTEXITCODE). Inspect 'wpr -status'. No policy override was attempted."}
    $ab2Started=$true
    Write-Host "$($ab2Profiles -join '/') capture active for $Seconds seconds. Play the normally launched game and reproduce the slowdown."
    for($ab2Remaining=$Seconds;$ab2Remaining -gt 0;--$ab2Remaining){
        Write-Progress -Activity 'Recording game CPU stalls' -Status "$ab2Remaining seconds remaining" -PercentComplete (100*($Seconds-$ab2Remaining)/$Seconds)
        Start-Sleep -Seconds 1
    }
}finally{
    if($ab2Started){
        & $ab2Wpr -stop $ab2Trace
        if($LASTEXITCODE -ne 0){throw "WPR could not save the trace. Inspect 'wpr -status' before leaving the session."}
        else{Write-Host "Trace saved: $ab2Trace"}
    }
    Write-Progress -Activity 'Recording game CPU stalls' -Completed
}
