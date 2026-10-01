#!/usr/bin/env bash
# Verify the module dependency direction: World <- Creatures <- Battle.
set -uo pipefail
fail=0
check() {           # $1 = consumer module, $2... = provider modules
  local consumer="$1"; shift
  local hdrs
  hdrs=$(for m in "$@"; do find "Source/$m" -name '*.h' -printf '%f\n' 2>/dev/null; done \
         | sed 's/\.h$//' | sort -u | paste -sd'|')
  [ -z "$hdrs" ] && return
  local bad
  bad=$(grep -rnE "#include \"($hdrs)\.h\"" "Source/$consumer" 2>/dev/null || true)
  if [ -n "$bad" ]; then
    echo "VIOLATION: $consumer includes headers from: $*"
    echo "$bad"
    fail=1
  else
    echo "ok: $consumer does not reach into $*"
  fi
}
check GammaFrameworkWorld     GammaFrameworkCreatures GammaFrameworkBattle
check GammaFrameworkCreatures GammaFrameworkBattle
exit $fail
