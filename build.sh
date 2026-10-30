#!/usr/bin/env bash
# One-command build: installs ufbt, fetches the SDK, builds geodash.fap into ./dist/
set -euo pipefail
cd "$(dirname "$0")"
python3 -m pip install --upgrade ufbt
ufbt update
ufbt
ls -l dist/geodash.fap
