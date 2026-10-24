#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
output="${1:-build/models}"
# This script is deliberately not called during the source-only development
# phase. Release preparation invokes it explicitly before configuring the app.
if ! xcrun --find coremlcompiler >/dev/null 2>&1 || ! xcodebuild -version >/dev/null 2>&1; then
    echo "Full Xcode is required to compile bundled models. Select Xcode.app with xcode-select or DEVELOPER_DIR; Command Line Tools alone are insufficient." >&2
    exit 1
fi
python3.12 -m venv .tools/model-venv
.tools/model-venv/bin/python -m pip install --disable-pip-version-check -r scripts/model-requirements.txt
.tools/model-venv/bin/python tests/models/test_denoise_conversion.py
.tools/model-venv/bin/python tests/models/test_denoise_quality.py
.tools/model-venv/bin/python tests/models/test_denoise_photographs.py
.tools/model-venv/bin/python scripts/convert-models.py --output "$output"
