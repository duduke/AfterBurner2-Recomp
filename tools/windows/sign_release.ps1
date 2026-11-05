# Sign an isolated runtime with an existing CA-issued code-signing certificate.
# Keys stay in the Windows certificate provider/HSM; no PFX or password inputs.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$ReceiptPath,
    [ValidatePattern('^[A-Fa-f0-9]{40}$')][string[]]$ApprovedRootThumbprint=@(),
    [ValidateSet('base','legacy')][string]$Profile='base',
    [string]$SignToolPath
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $ReceiptPath){throw 'Preserve the previous receipt; choose a new path'}
$cert=$null
$machine=$false
foreach($store in @('CurrentUser','LocalMachine')){
    $path='Cert:\'+$store+'\My\'+$CertificateThumbprint
    if(Test-Path $path){$cert=Get-Item $path;$machine=$store -eq 'LocalMachine';break}
}
if(-not $cert -or -not $cert.HasPrivateKey){throw 'Selected code-signing key is not available in this account/provider'}
if($cert.Subject -eq $cert.Issuer){throw 'A self-signed development certificate cannot sign a public release'}
if($cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -lt (Get-Date)){
    throw 'Selected signing certificate is outside its validity period'
}
if($cert.EnhancedKeyUsageList.ObjectId -notcontains '1.3.6.1.5.5.7.3.3'){
    throw 'Selected certificate is not enabled for code signing'
}
if(-not $SignToolPath){
    $command=Get-Command signtool.exe -ErrorAction SilentlyContinue
    if($command){$SignToolPath=$command.Source}else{
        $kits=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
        $candidate=Get-ChildItem $kits -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64\signtool.exe'} |
            Where-Object {Test-Path $_} | Select-Object -First 1
        $SignToolPath=$candidate
    }
}
if(-not $SignToolPath -or -not(Test-Path -LiteralPath $SignToolPath)){
    throw 'Install Windows SDK Signing Tools or provide -SignToolPath'
}
$verify=Join-Path $PSScriptRoot 'verify_release_signatures.ps1'
$before=$ReceiptPath+'.before.json'
# This verifies the complete allowlist before any file is changed. An invalid
# inventory throws without a receipt; unsigned inputs produce a failed receipt.
& $verify -RuntimeDirectory $RuntimeDirectory -ReceiptPath $before -Profile $Profile -ApprovedRootThumbprint $ApprovedRootThumbprint
if(-not(Test-Path -LiteralPath $before)){throw 'Runtime preflight did not produce a receipt'}
$audit=Get-Content -LiteralPath $before -Raw | ConvertFrom-Json
if(-not $audit.FullRuntime){throw 'Signing requires a complete runtime inventory'}
# Our signing identity may sign only our own executable. Vendor binaries
# must already carry valid upstream signatures; reject before modifying files.
if(@($audit.Files | Where-Object { $_.File -ne 'afterburner2.exe' -and -not $_.Passed }).Count){
    throw 'Unsigned upstream binaries require signed distributions from their maintainers'
}
foreach($file in $audit.Files){
    # Keep already trusted vendor signatures and their original timestamps.
    if($file.Passed){continue}
    $target=Join-Path $RuntimeDirectory $file.File
    if((Get-FileHash -LiteralPath $target).Hash -ne $file.SHA256){throw ('Input changed: '+$file.File)}
    $arguments=@('sign','/sha1',$CertificateThumbprint,'/fd','SHA256',
        '/tr','http://timestamp.digicert.com','/td','SHA256')
    if($machine){$arguments+='/sm'}
    & $SignToolPath @arguments $target
    if($LASTEXITCODE -ne 0){throw ('Signing failed: '+$file.File)}
}
& $verify -RuntimeDirectory $RuntimeDirectory -ReceiptPath $ReceiptPath -Profile $Profile -ApprovedRootThumbprint $ApprovedRootThumbprint
if($LASTEXITCODE -ne 0){throw 'Signed runtime did not pass public-signature verification'}
