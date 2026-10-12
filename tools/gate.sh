#!/usr/bin/env bash
# Gate de fim de agente. Uso: tools/gate.sh NN   (ex.: tools/gate.sh 03)
# Pré-requisito: ambiente do IDF ativo (idf.py no PATH) e árvore git commitada.
set -euo pipefail

N="${1:?uso: tools/gate.sh NN}"
H="docs/handoff/${N}-done.md"

[ -f "$H" ] || { echo "FALTA $H"; exit 1; }
[ "$(wc -l < "$H")" -le 80 ] || { echo "$H passa de 80 linhas: resuma"; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "árvore suja: commite antes do gate"; exit 1; }

[ -f tools/check_pins.sh ] && bash tools/check_pins.sh
[ -f test_host/Makefile ] && make -C test_host test

mkdir -p build
idf.py build 2>&1 | tee "build/gate-${N}.log"

W=$(grep -c "warning:" "build/gate-${N}.log" || true)
echo "warnings: ${W}"
BASE="docs/handoff/baseline-warnings.txt"
if [ -f "$BASE" ] && [ "$W" -gt "$(cat "$BASE")" ]; then
  echo "warnings subiram (baseline $(cat "$BASE"))"; exit 1
fi
[ -f "$BASE" ] || echo "$W" > "$BASE"

idf.py size | tee "docs/handoff/${N}-size.txt"

git add -A
git commit -qm "chore(gate): agente ${N} aprovado" || true
git tag -f "agent-${N}-done"
echo "GATE ${N} OK"
