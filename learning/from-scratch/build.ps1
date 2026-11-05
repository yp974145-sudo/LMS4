$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$build=Join-Path $PSScriptRoot 'build'
$temporary=Join-Path $PSScriptRoot '.tmp'
New-Item -ItemType Directory -Path $build,$temporary -Force | Out-Null
$previousTemp=$env:TEMP
$previousTmp=$env:TMP
try {
    $env:TEMP=$temporary
    $env:TMP=$temporary
    foreach($stage in @('00_hello','01_model','02_business','03_http','04_algorithms')) {
        & g++ -std=c++17 -O0 -g -pthread -I (Join-Path $project 'include') (Join-Path $PSScriptRoot "$stage.cpp") -lws2_32 -static -o (Join-Path $build "$stage.exe")
        if($LASTEXITCODE -ne 0){throw "Compile failed: $stage"}
    }
    foreach($stage in @('00_hello','01_model','02_business','04_algorithms')) {
        & (Join-Path $build "$stage.exe")
        if($LASTEXITCODE -ne 0){throw "Run failed: $stage"}
    }
} finally {
    $env:TEMP=$previousTemp
    $env:TMP=$previousTmp
}
