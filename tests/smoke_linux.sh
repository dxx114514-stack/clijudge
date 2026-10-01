#!/bin/bash
# CliJudge Linux smoke - report fixes + ide de-shell + custom language
set -u
BIN="${CIJUDGE_BIN:-$(cd "$(dirname "$0")/.." && pwd)/clijudge}"
export CLIJUDGE_DATA_DIR=/tmp/cj_lx_smoke/data
W=/tmp/cj_lx_smoke/work
rm -rf /tmp/cj_lx_smoke
mkdir -p "$CLIJUDGE_DATA_DIR" "$W"
pass=0; fail=0
t() { # t <name> <0=pass> [detail]
  if [ "$2" -eq 0 ]; then pass=$((pass+1)); echo "PASS: $1"; else fail=$((fail+1)); echo "FAIL: $1  ${3:-}"; fi
}
run() { OUT=$("$BIN" "$@" 2>&1); CODE=$?; }

# environment probes (diagnose CI vs local sandbox behavior)
export CLIJUDGE_SANDBOX_DEBUG=/tmp/cj_lx_smoke/sbx.dbg
echo "ENV: uname=$(uname -r)"
echo "ENV: apparmor_restrict_unprivileged_userns=$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null || echo n/a)"
echo "ENV: max_user_namespaces=$(cat /proc/sys/user/max_user_namespaces 2>/dev/null || echo n/a)"
if unshare -U true; then echo "ENV: unshare -U ok"; else echo "ENV: unshare -U DENIED rc=$?"; fi

run help
[ $CODE -eq 0 ] && echo "$OUT" | grep -q Usage && echo "$OUT" | grep -q displaylang; t "help exit0+usage+displaylang" $? "$OUT"

run problem create "A + B"
echo "$OUT" | grep -q "Problem created with ID: 1"; t "problem create" $? "$OUT"

printf '1 2\n' > "$W/in.txt"; printf '3\n' > "$W/out.txt"
run problem testdata 1 create "$W/in.txt" "$W/out.txt" 1000 256 50
echo "$OUT" | grep -q "Test case created with ID: 1"; t "testdata create" $? "$OUT"

cat > "$W/ac.cpp" <<'EOF'
#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}
EOF
run problem submit 1 "$W/ac.cpp" --as alice
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "submit AC" $? "code=$CODE $OUT"

run problem export 1 "$W/p1.zip"
echo "$OUT" | grep -q "Problem exported to:"; t "problem export" $? "$OUT"

run problem delete 1
echo "$OUT" | grep -q "Problem 1 deleted\."; t "problem delete EN" $? "$OUT"

run problem import "$W/p1.zip"
echo "$OUT" | grep -q "Problem imported with ID:"; t "problem import" $? "$OUT"
IMP=$(echo "$OUT" | sed -n 's/.*Problem imported with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$IMP" ]; t "import id captured ($IMP)" $? "$OUT"
echo "$OUT" | grep -q "Warning: this package"; r=$?; [ $r -ne 0 ]; t "import no-spj no warning" $? "$OUT"

cat > "$W/spj.json" <<EOF
{"problem":{"title":"SPJ P","time_limit":1000,"memory_limit":256,"problem_type":"traditional","compare_mode":"spj","spj_code":"int main(){return 0;}"},"test_cases":[]}
EOF
run problem import "$W/spj.json"
echo "$OUT" | grep -q "Problem imported with ID"; t "spj import" $? "$OUT"
echo "$OUT" | grep -q "Warning: this package contains special judge"; t "spj import warns trust" $? "$OUT"
echo "$OUT" | grep -q "reduced-isolation"; t "spj warning trusted mode" $? "$OUT"

run article create Hello
echo "$OUT" | grep -q "Article created, ID: 1"; t "article create EN" $? "$OUT"
run article delete 1
echo "$OUT" | grep -q "Article 1 deleted\."; t "article delete EN" $? "$OUT"
run article delete 9
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Article 9 not found\."; t "article notfound EN" $? "$OUT"

run contest create C1 "2026-01-01 10:00:00" "2026-01-02 10:00:00" "$IMP"
echo "$OUT" | grep -q "Contest created with ID: 1"; t "contest create" $? "$OUT"

run contest export 1 "$W/c.cdf"
echo "$OUT" | grep -q "Contest exported to:"; t "contest export EN" $? "$OUT"

run contest import
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge.exe contest import \[cdf_path\]"; t "contest import usage EN" $? "$OUT"
run contest export 1
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge.exe contest export \[id\] \[cdf_path\]"; t "contest export usage EN" $? "$OUT"

run contest import "$W/c.cdf"
echo "$OUT" | grep -q "Contest imported:"; t "contest import EN" $? "$OUT"
echo "$OUT" | grep -q "  Imported problem:"; t "contest import problem line EN" $? "$OUT"
echo "$OUT" | grep -q "Warning: this CDF"; r=$?; [ $r -ne 0 ]; t "contest import no-spj no warning" $? "$OUT"

cat > "$W/hello.cpp" <<'EOF'
#include <iostream>
int main(){std::cout<<"hi"<<std::endl;return 0;}
EOF
run ide run "$W/hello.cpp"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q hi && echo "$OUT" | grep -q "Exit code: 0" && echo "$OUT" | grep -q "Time: "; t "ide run c++" $? "code=$CODE $OUT"

run ide run "$W/ac.cpp" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3" && echo "$OUT" | grep -q "Exit code: 0"; t "ide run stdin redirect" $? "code=$CODE $OUT"

cat > "$W/t.py" <<'EOF'
import sys
a,b=map(int,sys.stdin.read().split())
print(a+b)
EOF
run ide run "$W/t.py" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide run python" $? "code=$CODE $OUT"

echo 'int main(){return}' > "$W/bad.cpp"
run ide run "$W/bad.cpp"
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "error:"; t "ide run compile error" $? "code=$CODE $OUT"

run ide run "$W/missing.cpp"
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Error: code file not found"; t "ide run missing file" $? "$OUT"

run contest report 1 "$W/rep.html"
echo "$OUT" | grep -q "Contest report generated:"; t "contest report EN" $? "$OUT"

# corrupt config -> parse warning
CFG="$CLIJUDGE_DATA_DIR/config.json"
CFG_ORIG=$(cat "$CFG")
echo '{ this is not json' > "$CFG"
run problem count
echo "$OUT" | grep -q "Warning: failed to parse .*config\.json"; t "corrupt config warns" $? "$OUT"
printf '%s' "$CFG_ORIG" > "$CFG"
run problem count
[ $CODE -eq 0 ] && ! echo "$OUT" | grep -q "Warning:"; t "restored config clean" $? "code=$CODE $OUT"

# custom language: compiled (g++ {exe}) via ide + submit
python3 - "$CFG" <<'PYEOF'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
cfg.setdefault('judge',{})['custom_languages']={'jl':{'extensions':['.jlang'],'compile':'g++ -O2 -x c++ -o {exe} {src}','run':'{exe}'}}
json.dump(cfg,open(p,'w'))
PYEOF
ck_rc=$?; t "config custom lang written" $ck_rc
cat > "$W/a.jlang" <<'EOF'
#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}
EOF
run ide run "$W/a.jlang" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide custom-lang compiled" $? "code=$CODE $OUT"

run problem create "JL"
echo "$OUT" | grep -q "Problem created with ID"; t "jl problem create" $? "$OUT"
JLID=$(echo "$OUT" | sed -n 's/.*ID: \([0-9]*\).*/\1/p' | head -1)
run problem testdata "$JLID" create "$W/in.txt" "$W/out.txt" 1000 256 50
echo "$OUT" | grep -q "Test case created"; t "jl testdata" $? "$OUT"
run problem submit "$JLID" "$W/a.jlang" --as bob
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "jl submit AC" $? "code=$CODE $OUT"

# custom language: bare command (PATH resolution), script-style (no compile)
mkdir -p /tmp/cj_lx_smoke/bin
cat > /tmp/cj_lx_smoke/bin/judgelang <<'EOF'
#!/bin/bash
# usage: judgelang <src>  - reads stdin, prints a+b
read a b
echo $((a+b))
EOF
chmod +x /tmp/cj_lx_smoke/bin/judgelang
python3 - "$CFG" <<'PYEOF'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
cfg.setdefault('judge',{})['custom_languages']={'bare':{'extensions':['.bglang'],'compile':'','run':'judgelang {src}'}}
json.dump(cfg,open(p,'w'))
PYEOF
export PATH=/tmp/cj_lx_smoke/bin:$PATH
cat > "$W/b.bglang" <<'EOF'
dummy source resolved by judgelang shim
EOF
run ide run "$W/b.bglang" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide custom-lang bare PATH" $? "code=$CODE $OUT"

run problem create "BG"
BGID=$(echo "$OUT" | sed -n 's/.*ID: \([0-9]*\).*/\1/p' | head -1)
run problem testdata "$BGID" create "$W/in.txt" "$W/out.txt" 1000 256 50
run problem submit "$BGID" "$W/b.bglang" --as bob
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "bare custom-lang submit AC" $? "code=$CODE $OUT"

echo ""
echo "PASS: $pass  FAIL: $fail"
if [ -s "$CLIJUDGE_SANDBOX_DEBUG" ]; then
  echo "--- sandbox debug log ---"
  sort -u "$CLIJUDGE_SANDBOX_DEBUG" | head -40
fi
rm -rf /tmp/cj_lx_smoke
[ $fail -eq 0 ] && exit 0 || exit 1
