$ErrorActionPreference = 'Stop'

$preset = 'default'

Write-Host ''
$cmakeArgs = @('--preset', $preset, '-B', 'build', '-S', '.', '--fresh')

$pluginDir = (Read-Host 'Enter Mod plugin directory (leave blank to keep CMakeLists.txt default)').Trim()
if (-not [string]::IsNullOrWhiteSpace($pluginDir)) {
    Write-Host "Using Mod plugin directory: $pluginDir"
    $cmakeArgs += "-DMMOHOTBAR_DEPLOY_DIR=$pluginDir"
}
else {
    Write-Host 'Using CMakeLists.txt deploy directory default.'
}

Push-Location $PSScriptRoot
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
