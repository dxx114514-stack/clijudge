# CliJudge smoke regression - covers report fixes (EN output, ODR inline, import trust
# warning, config parse warning, usage strings, contest/article roundtrip, ide de-shell)
param(
    [string]$Bin = (Join-Path (Split-Path -Parent $PSScriptRoot) "clijudge.exe")
)
$ErrorActionPreference = "Continue"
$root = "$env:TEMP\cj_smoke_" + (Get-Random)
$data = "$root\data"
$work = "$root\work"
New-Item -ItemType Directory -Path $data, $work -Force | Out-Null
$env:CLIJUDGE_DATA_DIR = $data

$script:pass = 0
$script:fail = 0

function Run($argList) {
    $out = (& $bin @argList 2>&1 | Out-String)
    return @{ out = $out; code = $LASTEXITCODE }
}
function Check($name, $cond, $detail = "") {
    if ($cond) { $script:pass++; Write-Host "PASS: $name" }
    else { $script:fail++; Write-Host "FAIL: $name  $detail" }
}
function W($path, $content) { [IO.File]::WriteAllText($path, $content) }

# T01 help + displaylang line
$r = Run @("help")
Check "help exit0" ($r.code -eq 0) "code=$($r.code)"
Check "help has displaylang" ($r.out -match "displaylang") ""
Check "help has Usage" ($r.out -match "Usage") ""

# T02 problem create
$r = Run @("problem", "create", "A + B")
Check "problem create" ($r.out -match "Problem created with ID: 1") $r.out

# T03 testdata (LF files, byte-exact compare)
W "$work\in.txt" "1 2`n"
W "$work\out.txt" "3`n"
$r = Run @("problem", "testdata", "1", "create", "$work\in.txt", "$work\out.txt", "1000", "256", "50")
Check "testdata create" ($r.out -match "Test case created with ID: 1") $r.out

# T04 submit AC
W "$work\ac.cpp" '#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}'
$r = Run @("problem", "submit", "1", "$work\ac.cpp", "--as", "alice")
Check "submit AC" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

# T05 export
$r = Run @("problem", "export", "1", "$work\p1.zip")
Check "problem export" ($r.out -match "Problem exported to:") $r.out

# T06 delete
$r = Run @("problem", "delete", "1")
Check "problem delete EN" ($r.out -match "Problem 1 deleted\.") $r.out

# T07 import zip without SPJ -> no trust warning (capture dynamic id)
$r = Run @("problem", "import", "$work\p1.zip")
Check "problem import" ($r.out -match "Problem imported with ID: \d+") $r.out
$impId = 0
if ($r.out -match "Problem imported with ID: (\d+)") { $impId = [int]$Matches[1] }
Check "import id captured" ($impId -gt 0) "impId=$impId"
Check "import no-spj no warning" (-not ($r.out -match "Warning: this package")) $r.out

# T08 import json WITH spj_code -> trust warning
$spjJson = @{
    problem    = @{
        title = "SPJ P"; time_limit = 1000; memory_limit = 256
        problem_type = "traditional"; compare_mode = "spj"
        spj_code     = "int main(){return 0;}"
    }
    test_cases = @()
} | ConvertTo-Json -Depth 6
W "$work\spj.json" $spjJson
$r = Run @("problem", "import", "$work\spj.json")
Check "spj import" ($r.out -match "Problem imported with ID") $r.out
Check "spj import warns trust" ($r.out -match "Warning: this package contains special judge") $r.out
Check "spj warning mentions trusted mode" ($r.out -match "reduced-isolation") $r.out

# T09/T10 article EN outputs
$r = Run @("article", "create", "Hello")
Check "article create EN" ($r.out -match "Article created, ID: 1") $r.out
$r = Run @("article", "delete", "1")
Check "article delete EN" ($r.out -match "Article 1 deleted\.") $r.out
$r = Run @("article", "delete", "9")
Check "article notfound EN" (($r.code -eq 1) -and ($r.out -match "Article 9 not found\.")) $r.out

# T11 contest create with the dynamically imported problem id
$r = Run @("contest", "create", "C1", "2026-01-01 10:00:00", "2026-01-02 10:00:00", "$impId")
Check "contest create" ($r.out -match "Contest created with ID: 1") $r.out

# T12 contest export
$r = Run @("contest", "export", "1", "$work\c.cdf")
Check "contest export EN" ($r.out -match "Contest exported to:") $r.out

# T13 usage errors EN
$r = Run @("contest", "import")
Check "contest import usage EN" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge\.exe contest import \[cdf_path\]")) $r.out
$r = Run @("contest", "export", "1")
Check "contest export usage EN" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge\.exe contest export \[id\] \[cdf_path\]")) $r.out

# T14 contest import roundtrip (EN outputs, no warning for plain problem)
$r = Run @("contest", "import", "$work\c.cdf")
Check "contest import EN" ($r.out -match "Contest imported:") $r.out
Check "contest import problem line EN" ($r.out -match "  Imported problem:") $r.out
Check "contest import no-spj no warning" (-not ($r.out -match "Warning: this CDF")) $r.out

# T15 ide run EN outputs, stdin redirect (de-shell redesign)
W "$work\hello.cpp" '#include <iostream>
int main(){std::cout<<"hi"<<std::endl;return 0;}'
$r = Run @("ide", "run", "$work\hello.cpp")
Check "ide run no-input exit0" (($r.code -eq 0) -and ($r.out -match "hi")) "code=$($r.code) out=$($r.out)"
Check "ide run EN lines" (($r.out -match "Exit code: 0") -and ($r.out -match "Time: ") -and ($r.out -match "Memory: ")) $r.out
$r = Run @("ide", "run", "$work\ac.cpp", "$work\in.txt")
Check "ide run stdin redirect" (($r.code -eq 0) -and ($r.out -match "^3") -and ($r.out -match "Exit code: 0")) "code=$($r.code) out=$($r.out)"
W "$work\t.py" "import sys`na,b=map(int,sys.stdin.read().split())`nprint(a+b)"
$r = Run @("ide", "run", "$work\t.py", "$work\in.txt")
Check "ide run python" (($r.code -eq 0) -and ($r.out -match "^3")) "code=$($r.code) out=$($r.out)"
W "$work\bad.cpp" "int main(){return}"
$r = Run @("ide", "run", "$work\bad.cpp")
Check "ide run compile error" (($r.code -eq 1) -and ($r.out -match "error:")) "code=$($r.code) out=$($r.out)"
$r = Run @("ide", "run", "$work\missing.cpp")
Check "ide run missing file" (($r.code -eq 1) -and ($r.out -match "Error: code file not found")) $r.out

# T16 report generated EN
$r = Run @("contest", "report", "1", "$work\rep.html")
Check "contest report EN" ($r.out -match "Contest report generated:") $r.out

# T16b custom language (compiled) via ide + submit - new de-shell chain
$cfgPath = "$data\config.json"
W $cfgPath '{"current_lang":"en","judge":{"custom_languages":{"jl":{"extensions":[".jlang"],"compile":"g++ -O2 -x c++ -o {exe} {src}","run":"{exe}"}}}}'
W "$work\a.jlang" '#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}'
$r = Run @("ide", "run", "$work\a.jlang", "$work\in.txt")
Check "ide custom-lang compiled" (($r.code -eq 0) -and ($r.out -match "^3")) "code=$($r.code) out=$($r.out)"
$r = Run @("problem", "create", "JL")
$jlId = 0
if ($r.out -match "Problem created with ID: (\d+)") { $jlId = [int]$Matches[1] }
Check "jl problem create" ($jlId -gt 0) $r.out
$r = Run @("problem", "testdata", "$jlId", "create", "$work\in.txt", "$work\out.txt", "1000", "256", "50")
Check "jl testdata" ($r.out -match "Test case created") $r.out
$r = Run @("problem", "submit", "$jlId", "$work\a.jlang", "--as", "bob")
Check "jl submit AC" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

# T17 corrupt config.json -> parse warning (LAST: restore after)
$cfgPath = "$data\config.json"
$cfgOrig = [IO.File]::ReadAllText($cfgPath)
W $cfgPath "{ this is not json"
$r = Run @("problem", "count")
Check "corrupt config warns" ($r.out -match "Warning: failed to parse .*config\.json") $r.out
[IO.File]::WriteAllText($cfgPath, $cfgOrig)
$r = Run @("problem", "count")
Check "restored config clean" (($r.code -eq 0) -and (-not ($r.out -match "Warning:"))) "code=$($r.code) out=$($r.out)"

Write-Host ""
Write-Host "PASS: $script:pass  FAIL: $script:fail"
Remove-Item -Path $root -Recurse -Force -ErrorAction SilentlyContinue
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
