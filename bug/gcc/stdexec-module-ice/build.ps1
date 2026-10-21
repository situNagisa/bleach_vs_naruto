param(
	[string]$Compiler = 'g++',
	[string]$StdexecInclude = $env:STDEXEC_INCLUDE
)
$ErrorActionPreference = 'Stop'
if (-not $StdexecInclude) {
	throw 'Pass -StdexecInclude <stdexec/include> or set STDEXEC_INCLUDE.'
}
$StdexecInclude = (Resolve-Path -LiteralPath $StdexecInclude).Path
$compiler_path = (Get-Command $Compiler -ErrorAction Stop).Source
$output_dir = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $output_dir | Out-Null
Push-Location $output_dir
try {
	& $compiler_path --version
	& $compiler_path '-std=c++26' '-fsyntax-only' "-I$StdexecInclude" (Join-Path $PSScriptRoot 'header.cpp') *> header.log
	$header_exit = $LASTEXITCODE
	& $compiler_path '-std=c++26' '-fmodules' '-x' 'c++' '-c' "-I$StdexecInclude" (Join-Path $PSScriptRoot 'module.cpp') '-o' 'module.o' *> module.log
	$module_exit = $LASTEXITCODE
	Write-Output "Header control exit: $header_exit"
	Write-Output "Module repro exit: $module_exit"
	Get-Content module.log
} finally {
	Pop-Location
}
if ($header_exit -ne 0) { exit $header_exit }
exit $module_exit
