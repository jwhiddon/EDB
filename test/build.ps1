$ErrorActionPreference = "Stop"

$compiler = $null
foreach ($candidate in @("g++", "clang++")) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) {
        $compiler = $candidate
        break
    }
}

if (-not $compiler) {
    Write-Error "g++ or clang++ is required to run native tests"
}

Push-Location $PSScriptRoot
try {
    python ..\tools\generate_fixtures.py
    & $compiler -std=c++11 -Wall -Wextra -I../ -Isupport -DEDB_TEST `
        test_main.cpp test_edb.cpp test_edb_integrity.cpp test_api_compat.cpp ../EDB.cpp support/unity.c `
        -o test_edb.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\test_edb.exe
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
