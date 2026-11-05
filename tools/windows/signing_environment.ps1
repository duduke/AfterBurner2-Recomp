# Use only modules from the active PowerShell installation. Inherited
# PSModulePath from PowerShell 7 can otherwise break a Windows PowerShell 5 child.
# This registers built-in commands/providers only; it changes no trust or policy.
foreach($ab2Module in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility','Microsoft.PowerShell.Security')){
    $ab2Manifest=[IO.Path]::Combine($PSHOME,'Modules',$ab2Module,($ab2Module+'.psd1'))
    Import-Module $ab2Manifest -ErrorAction Stop
}
Get-PSDrive -Name Cert -ErrorAction Stop | Out-Null
Get-Command -Name @('Get-FileHash','Get-AuthenticodeSignature','ConvertTo-Json',
    'Get-Item','Get-ChildItem','Join-Path','Get-Date','Sort-Object',
    'Format-Table','Get-Content','Set-Content') -ErrorAction Stop | Out-Null
Write-Output ('Signing environment ready: PowerShell '+$PSVersionTable.PSVersion.ToString())
