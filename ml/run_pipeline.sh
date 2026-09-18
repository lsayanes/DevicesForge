#!/usr/bin/env bash
# Ejecuta los 4 pasos del laboratorio IA en orden.
# Uso (desde ml/):
#   source .venv/bin/activate
#   ./run_pipeline.sh

set -euo pipefail
cd "$(dirname "$0")"

echo "== Paso 1: datos sintéticos =="
python3 scripts/generate_synthetic_pairs.py

echo ""
echo "== Paso 2: entrenar =="
python3 scripts/train_unet_denoise.py

echo ""
echo "== Paso 3: export ONNX =="
python3 scripts/export_onnx.py

echo ""
echo "== Paso 4: evaluar =="
python3 scripts/evaluate_ir.py

echo ""
echo "Pipeline completo."
