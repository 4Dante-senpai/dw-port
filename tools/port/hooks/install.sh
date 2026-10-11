#!/usr/bin/env bash
# Installs the versioned git hooks by pointing core.hooksPath at this directory.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
git config core.hooksPath tools/port/hooks
echo "hooks installed: core.hooksPath=tools/port/hooks"
