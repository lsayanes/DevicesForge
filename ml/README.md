# DevicesForge — laboratorio IA (Fase 5, opcional)

**Guía para empezar (recomendada):** [`docs/AI-PHASE3-GUIA.md`](../docs/AI-PHASE3-GUIA.md)

**Spec técnica:** [`docs/AI-PHASE3.md`](../docs/AI-PHASE3.md)

El plugin VST3 **no ejecuta Python**. Solo usa el archivo `models/ir_denoise_v1.onnx` generado aquí.

## Comandos rápidos

```bash
cd ml
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
./run_pipeline.sh
```

O paso a paso:

```bash
python3 scripts/generate_synthetic_pairs.py
python3 scripts/train_unet_denoise.py
python3 scripts/export_onnx.py
python3 scripts/evaluate_ir.py
```

## Estructura

```text
ml/
  df_stft.py              # STFT (compartido)
  unet_model.py           # arquitectura U-Net
  scripts/                # 4 pasos lineales (leer en orden)
  data/synthetic/         # generado (gitignored)
  checkpoints/            # .pt del entrenamiento (gitignored)
  models/                 # .onnx + evaluate_report.txt
```

## Estado

Scripts listos para experimentar. **No es prioridad del roadmap:** primero Fase 3 (convolución en el plugin y pruebas A/B). Integración C++ cuando haya go/no-go sobre IA.

