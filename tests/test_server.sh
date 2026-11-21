#!/bin/sh
set -e
cd "$(dirname "$0")/.."
rm -f accounts.dat transactions.log

out=$(./bank 3)
echo "$out"

fail=0
check() {
    if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: expected $2, got $3"; fail=1; fi
}

check "response count"       24 "$(echo "$out" | wc -l)"
check "success (OK)"         15 "$(echo "$out" | grep -c 'status=0 ')"
check "wrong password"        3 "$(echo "$out" | grep -c 'status=-2 ')"
check "insufficient balance"  3 "$(echo "$out" | grep -c 'status=-3 ')"
check "bad request"           3 "$(echo "$out" | grep -c 'status=-10 ')"

bal() { od -An -t d8 -j $(( $1 * 88 + 72 )) -N 8 accounts.dat | tr -d ' '; }
check "1001 final balance"   501200 "$(bal 0)"
check "1002 final balance"   300300 "$(bal 1)"

exit $fail
