param(
    [string]$SwigPath = "",
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if (!$SwigPath) { $SwigPath = Join-Path $repo "swigwin-4.5.1/swigwin-4.5.1/swig.exe" }
if (!$OutputDirectory) { $OutputDirectory = $PSScriptRoot }
$null = New-Item -ItemType Directory -Force $OutputDirectory
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$encoding = [System.Text.UTF8Encoding]::new($false)
function Write-Crlf([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, ($Text -replace "\r?\n", "`r`n"), $encoding)
}
$headers = @{}
foreach ($library in @("LWS", "LWSUI")) {
    $root = Join-Path $repo "src/$library/include"
    foreach ($file in Get-ChildItem -LiteralPath $root -Filter *.hpp -Recurse) {
        $relative = [System.IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        if ($relative.StartsWith('LWS/Wayland/')) { continue }
        $headers[$relative] = $file.FullName
    }
}
$visited = @{}
$ordered = [System.Collections.Generic.List[string]]::new()
function Visit-Header([string]$Name) {
    if ($visited.ContainsKey($Name)) { return }
    $visited[$Name] = $true
    $content = [System.IO.File]::ReadAllText($headers[$Name])
    foreach ($match in [regex]::Matches($content, '#include\s*[<"]([^>"]+)[>"]')) {
        $dependency = $match.Groups[1].Value
        if ($headers.ContainsKey($dependency)) { Visit-Header $dependency }
    }
    $ordered.Add($Name)
}
foreach ($name in ($headers.Keys | Sort-Object)) { Visit-Header $name }
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('%module lws_c')
$lines.Add('%feature("nspace");')
$lines.Add('%include <stdint.i>')
$lines.Add('%include <std_string.i>')
$lines.Add('%include "' + (Join-Path $PSScriptRoot 'ownership.i').Replace('\', '/') + '"')
$lines.Add('#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(Enum)')
$lines.Add('%{')
foreach ($name in $ordered) { $lines.Add('#include <' + $name + '>') }
$lines.Add('%}')
foreach ($name in $ordered) { $lines.Add('%include <' + $name + '>') }
$parserRoot = Join-Path $OutputDirectory "parser"
foreach ($name in $ordered) {
    $parserPath = Join-Path $parserRoot $name
    $null = New-Item -ItemType Directory -Force (Split-Path $parserPath)
    $content = [IO.File]::ReadAllText($headers[$name])
    # SWIG cannot parse defaulted spaceship operators. They have no C equivalent.
    $content = $content -replace '(?m)^\s*auto operator<=>[^\r\n]*', ''
    $content = $content -replace '\) noexcept>', ')>'
    $content = $content -replace '(?s)T step = \[\]\s*\{.*?\}\(\);', 'T step;'
    $content = $content -replace 'uint32_t\{(0x[0-9a-fA-F]+)\}', '$1'
    if ($name -eq 'LWSUI/Theme.hpp') { $content = $content -replace '\{(?:0x[0-9a-fA-F]+|[0-9][^{}\r\n]*)\}', '' }
    $content = $content.Replace('const auto& Children()', 'const std::vector<std::unique_ptr<Control>>& Children()')
    $content = $content -replace '\b(\w+)\{\}(?=\s*[,;])', '$1'
    $content = $content -replace '\b(\w+)\{(0x[0-9a-fA-F]+)\}(?=\s*[,;])', '$1'
    Write-Crlf $parserPath $content
}
$interface = Join-Path $OutputDirectory "lws_c.i"
Write-Crlf $interface (($lines -join "`n") + "`n")
$arguments = @('-c++', '-c', '-nocxx', ('-I' + $parserRoot), '-std=c++23', '-DLWS_PLATFORM_WIN32', '-DLWS_HAS_WIN32_BACKEND', '-D_WIN32', '-DUNICODE', '-D_UNICODE', ('-I' + (Join-Path $repo 'src/LWS/include')), ('-I' + (Join-Path $repo 'src/LWSUI/include')), ('-I' + (Join-Path $repo 'external/LLUtils/Include')), '-o', (Join-Path $OutputDirectory 'lws_c_wrap.cxx'), $interface)
$messages = & $SwigPath @arguments 2>&1
$code = $LASTEXITCODE
Write-Crlf (Join-Path $OutputDirectory 'generation.log') (($messages -join "`n") + "`n")
$messages | ForEach-Object { Write-Host $_ }
if ($code -ne 0) { throw "SWIG failed with exit code $code" }
# SWIG's experimental C backend drops readonly features on inherited fields.
# Remove only setters whose generated argument is a noncopyable UI event.
$sourcePath = Join-Path $OutputDirectory 'lws_c_wrap.cxx'
$headerPath = Join-Path $OutputDirectory 'lws_c_wrap.h'
$source = [IO.File]::ReadAllText($sourcePath)
$header = [IO.File]::ReadAllText($headerPath)
$setters = [regex]::Matches($source, '(?ms)^SWIGEXPORTC void (\w+_set)\([^\r\n]*\) \{\r?\n(?:(?!^\}).)*^\}')
$removed = 0
foreach ($setter in $setters) {
    if ($setter.Value -notmatch 'LWSUI::Event\s*<') { continue }
    $name = $setter.Groups[1].Value
    $source = $source.Replace($setter.Value, '')
    $declaration = '(?m)^SWIGIMPORT void ' + [regex]::Escape($name) + '\([^\r\n]*\);\r?\n'
    if (![regex]::IsMatch($header, $declaration)) { throw "Missing declaration for event setter $name" }
    $header = [regex]::Replace($header, $declaration, '')
    $removed++
}
# The C backend qualifies enum references but emits unqualified enum definitions.
$enumDefinitions = @([regex]::Matches($header, '\benum\s+(\w+)\s*\{') | ForEach-Object { $_.Groups[1].Value })
$enumReferences = @([regex]::Matches($header, '\benum\s+(\w+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
foreach ($reference in $enumReferences) {
    if ($enumDefinitions -contains $reference) { continue }
    $matches = @($enumDefinitions | Where-Object { $reference.EndsWith('_' + $_) })
    if ($matches.Count -ne 1) { throw "Cannot resolve generated enum reference $reference" }
    $header = $header -replace ('\benum\s+' + [regex]::Escape($reference) + '\b'), ('enum ' + $matches[0])
}
Write-Crlf $sourcePath $source
Write-Crlf $headerPath $header
Write-Host "Removed $removed invalid event setters. Event getters remain borrowed."
foreach ($file in Get-ChildItem -LiteralPath $OutputDirectory -Filter 'lws_c_wrap.*') {
    $output = [IO.File]::ReadAllText($file.FullName) -replace '(?m)[ \t]+(?=\r?$)', ''
    Write-Crlf $file.FullName ($output.TrimEnd("`r", "`n") + "`r`n")
}
Write-Host "Generated C wrappers in $OutputDirectory"
