## Aviso:
### Esta parte del proyecto fue desarrollada completamente con IA, use Composer 2.5 Fast en su mayoria y algo de Cursor Grok 4.6 desde Cursor. Este documento es parte de una discusion acerca del por que usar Python, lenguaje que me resulta incomprensible y "automagico" en lugar de C++, por esta razon tanto este documento como [`AI-PHASE3.md`](AI-PHASE3.md) (escritos por estos modelos) tienen ese tono explicativo 

# Guía Fase 3 IA — sin ser experto en Python

Esta guía es la **entrada amigable**. Los detalles técnicos están en [`AI-PHASE3.md`](AI-PHASE3.md).

## Idea en una frase

El **plugin (C++)** usa un archivo **`ir_denoise_v1.onnx`** que es una “caja” entrenada para limpiar la IR.  
El **laboratorio (`ml/`)** crea ese archivo **una vez** (o cuando quieras mejorar el modelo).  
No tenés que programar Python en el día a día del VST3.

## Qué hace cada paso (los 4 scripts)

| Orden | Script | Analogía |
|-------|--------|----------|
| 1 | `generate_synthetic_pairs.py` | Fabricar muchas IR “limpias” y versiones “sucias” con ruido |
| 2 | `train_unet_denoise.py` | Enseñar a la red: dado sucio → predecir limpio |
| 3 | `export_onnx.py` | Guardar la red en formato que entiende el plugin |
| 4 | `evaluate_ir.py` | Medir si mejora (números SNR) antes de tocar C++ |

Solo hay **cuatro archivos** que leer en [`ml/scripts/`](../ml/scripts/). Cada uno tiene comentarios en español **de arriba abajo**.

Archivos auxiliares (podés ignorarlos al principio):

- [`ml/df_stft.py`](../ml/df_stft.py) — matemática STFT compartida (como una `.h` en C++).
- [`ml/unet_model.py`](../ml/unet_model.py) — capas de la red (como un `.h` de la U-Net).

## Cómo ejecutarlo la primera vez

```bash
cd ml
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
./run_pipeline.sh
```

Tiempo orientativo en Mac (CPU): unos minutos (depende de PyTorch).

Al terminar deberías tener:

- `ml/models/ir_denoise_v1.onnx`
- `ml/models/evaluate_report.txt` con mejora de SNR

## Copiar el modelo para el plugin (cuando exista integración C++)

```bash
mkdir -p "$HOME/Library/Application Support/DevicesForge/models"
cp ml/models/ir_denoise_v1.onnx "$HOME/Library/Application Support/DevicesForge/models/"
```

## Qué **no** necesitás entender de Python

- Decoradores, metaclases, async, etc.
- Todo el ecosistema PyTorch.

## Qué **sí** ayuda mirar (5 minutos)

1. En `generate_synthetic_pairs.py`: funciones `make_clean_ir` y `add_noise` — **qué datos inventamos**.
2. En `train_unet_denoise.py`: el bucle `for epoch` — **entrenar = repetir y bajar error L1**.
3. En `export_onnx.py`: nombres `magnitude_log` / `magnitude_log_clean` — **contrato con C++**.

## Si algo falla

| Mensaje | Acción |
|---------|--------|
| No hay pares WAV | Correr paso 1 primero |
| No existe `unet_best.pt` | Correr paso 2 |
| No existe `.onnx` | Correr paso 3 |
| Mejora SNR &lt; 1 dB | Normal al inicio: el MVP entrena en **parches**; subir `EPOCHS`, `NUM_TRAIN` o ajustar datos. La calidad “de oído” se afina iterando **después** de integrar C++ |

## Después de esta guía (Fase 3 en C++)

Integrar STFT + ONNX en `processCompletedCapture` cuando el parámetro **AI** está On. Eso es trabajo en **C++**, no en Python.

---

Ver también: [`ml/README.md`](../ml/README.md)
