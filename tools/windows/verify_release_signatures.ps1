# Verify the exact staged runtime before publication. This never changes trust
# stores, signs files, or changes Smart App Control. A passing receipt still
# requires an extracted-package launch/shader check with protection On.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$ReceiptPath,
    # Public CA roots confirmed out of band with the signing provider, not roots
    # created/imported for development. AuthRoot members are accepted by default.
    [ValidatePattern('^[A-Fa-f0-9]{40}$')][string[]]$ApprovedRootThumbprint=@(),
    # Only for dependency/negative-control checks; never a full-runtime receipt.
    [ValidateSet('base','legacy')][string]$Profile='base',
    [switch]$FilesOnly
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $ReceiptPath){throw 'Preserve the previous receipt; choose a new path'}
$directory=Get-Item -LiteralPath $RuntimeDirectory
if(-not $directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)){
    throw 'Runtime must be a regular directory'
}
$expected=@('afterburner2.exe','SDL3.dll','OpenAL32.dll','libc++.dll','libunwind.dll',
    'librashader.dll','MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll',
    'D3DX9_43.dll','D3DCompiler_43.dll')
if($Profile -eq 'base'){$expected=@('afterburner2.exe')}
$entries=@(Get-ChildItem -LiteralPath $directory.FullName -Force)
if($entries | Where-Object {$_.PSIsContainer -or ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)}){
    throw 'Runtime must be flat and contain no directories or links'
}
$files=@($entries | Where-Object Extension -in @('.exe','.dll'))
if(-not $files.Count){throw 'No PE files found'}
if(-not $FilesOnly){
    $difference=Compare-Object ($expected+@('Launch.cmd','LICENSES.txt')) @($entries.Name)
    if($difference){throw 'Runtime inventory differs from the release allowlist'}
}
# Microsoft built-in roots are not necessarily in AuthRoot. Published at:
# https://www.microsoft.com/pkiops/docs/repository.htm
$roots=@('3B1EFD3A66EA28B16697394703A72CA340A05BD5',
    '8F43288AD272F3103B6FB1428485EA3014C0BCFE')
$roots+=@(Get-ChildItem Cert:\LocalMachine\AuthRoot | ForEach-Object Thumbprint)
$roots+=$ApprovedRootThumbprint
$results=@()
foreach($file in $files | Sort-Object Name){
    $before=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    $signature=Get-AuthenticodeSignature -LiteralPath $file.FullName
    $failures=@()
    $rootThumbprint=$null
    if($signature.Status -ne 'Valid'){$failures+='Authenticode status: '+$signature.Status}
    $cert=$signature.SignerCertificate
    if(-not $cert){$failures+='Missing signer certificate'}else{
        if($cert.Subject -eq $cert.Issuer){$failures+='Self-signed development certificate'}
        $chain=New-Object Security.Cryptography.X509Certificates.X509Chain
        try {
            # WinVerifyTrust above validates Authenticode including signing time.
            # This separate chain walk only identifies its root, allowing old
            # vendor signatures whose signer expired after valid timestamping.
            $chain.ChainPolicy.VerificationFlags=[Security.Cryptography.X509Certificates.X509VerificationFlags]::IgnoreNotTimeValid
            $chain.ChainPolicy.RevocationMode=[Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
            [void]$chain.Build($cert)
            $root=$chain.ChainElements[$chain.ChainElements.Count-1].Certificate
            $rootThumbprint=$root.Thumbprint
            if($root.Subject -ne $root.Issuer -or $rootThumbprint -notin $roots){
                $failures+='Root is not in the approved public-root set'
            }
        }finally{$chain.Dispose()}
    }
    if(-not $signature.TimeStamperCertificate){$failures+='Missing trusted timestamp'}
    if((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $before){
        $failures+='File changed during verification'
    }
    $results+=[PSCustomObject]@{File=$file.Name;SHA256=$before;Passed=($failures.Count -eq 0);
        Status=[string]$signature.Status;Signer=$cert.Subject;SignerThumbprint=$cert.Thumbprint;
        RootThumbprint=$rootThumbprint;Timestamped=[bool]$signature.TimeStamperCertificate;
        Failures=$failures}
}
$passed=@($results | Where-Object {-not $_.Passed}).Count -eq 0
$policy=Get-ItemPropertyValue 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy' VerifiedAndReputablePolicyState -ErrorAction SilentlyContinue
$receipt=[PSCustomObject]@{Schema=1;UTC=[DateTime]::UtcNow.ToString('o');Passed=$passed;
    FullRuntime=(-not [bool]$FilesOnly);Profile=$Profile;SmartAppControlState=$policy;Files=$results;
    Scope='Authenticode, timestamps and approved public roots; launch/shader acceptance is separate'}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReceiptPath -Encoding UTF8
$results | Select-Object File,Passed,Status | Format-Table -AutoSize
if(-not $passed){exit 1}
exit 0
