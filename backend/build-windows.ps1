param([string]$TemporaryRoot='C:\Users\23974\Desktop\jk\x')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$prefix=Join-Path $project 'dependencies/msys2/ucrt64'
$cmake=Join-Path $project 'dependencies/cmake-3.31.6-windows-x86_64/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) {throw 'Install the documented Drogon dependencies first.'}
$temporary=Join-Path $TemporaryRoot ('lms4-drogon-build-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$oldPath=$env:PATH; $oldTemp=$env:TEMP; $oldTmp=$env:TMP
Push-Location $PSScriptRoot
try {
    $env:PATH=(Join-Path $prefix 'bin')+';C:\msys64\ucrt64\bin;'+$oldPath
    $env:TEMP=$temporary; $env:TMP=$temporary
    & $cmake --preset windows-mingw
    if($LASTEXITCODE -ne 0){throw 'Configure failed'}
    & $cmake --build --preset windows-mingw
    if($LASTEXITCODE -ne 0){throw 'Build failed'}
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --preset windows-mingw
    if($LASTEXITCODE -ne 0){throw 'HTTP check failed'}
} finally {
    Pop-Location
    $env:PATH=$oldPath; $env:TEMP=$oldTemp; $env:TMP=$oldTmp
    $target=[IO.Path]::GetFullPath($temporary)
    $root=[IO.Path]::GetFullPath($TemporaryRoot).TrimEnd('\')+'\'
    if($target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}
