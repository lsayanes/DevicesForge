# Export multi-formato

Tras una captura completa con retorno cableado, DevicesForge escribe archivos en:

```text
~/Documents/DevicesForge/exports/latest/
```

(macOS; en Windows: `%USERPROFILE%\\Documents\\DevicesForge\\exports\\latest\\`).

## Archivos automáticos (al terminar captura + IR)

| Archivo | Formato | Uso |
|---------|---------|-----|
| `IR.wav` | WAV PCM **24-bit**, sample rate del proyecto | Convolvers / DAW |
| `IR.float.wav` | WAV **IEEE float 32-bit** | Máxima precisión |
| `IR.aiff` | AIFF PCM **24-bit**, **96 kHz** (remuestreo lineal si el proyecto no está a 96 k) | macOS / Pro Tools |
| `IR.dfir` | Binario interno (`DFIR` + float32) | Plugin / herramientas DevicesForge |
| `capture_raw.float.wav` | Grabación mono pre/post (float) | Depuración del loop |

Si `capture_raw` está en **silencio**, el plugin **borra** los `IR.*` viejos y escribe `capture_log.txt` (pico, duración real, motivo). El VU de Cubase en el canal **no** implica que el insert reciba audio: en Cubase la pista necesita **Monitor ON** para que la entrada en vivo pase por los inserts (ver [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md) § Excepción de Cubase).

Duración de `capture_raw`: **no** es el valor de Duration. Es pre 100 ms + post `max(1 s, Duration + 0.25 s)` → con Duration 1 s ≈ **1.35 s**.

Parámetro de solo lectura **InPeak**: si durante Generate queda en 0, el plugin no está viendo el loop.

`IR.aiff` está a **96 kHz**. En un proyecto a 48 kHz, si Cubase no remuestrea, se oye al **doble de duración** y la mitad de frecuencia.

## Export manual (Cubase)

Parámetros VST:

- **ExportFmt**: WAV24 / WAV32f / AIFF96 / DFIR
- **Export**: flanco Off → On exporta la **última IR** en memoria al formato elegido (misma carpeta `latest/`).

Cada captura exitosa vuelve a generar **todos** los formatos de IR automáticamente.

## Formato binario `.dfir`

```text
Offset 0:  magic "DFIR"
           version (uint32) = 1
           sampleRate (uint32)
           numSamples (uint32)
           flags (uint32) = 0
           samples: numSamples × float32 LE
```

Ver `IRBinaryHeader` en [`IRExporter.h`](../src/dsp/IRExporter.h).

## Prueba tangible

1. Loop de interfaz o retorno del dispositivo → **entrada** de la pista.
2. **Generate** (sweep ~1 s).
3. Abrir `~/Documents/DevicesForge/exports/latest/` y comprobar `IR.wav`, `capture_raw.float.wav`, etc.
4. Cargar `IR.wav` en un convolver o en un editor de audio.

Sin retorno en el **Stereo In** del plugin, `capture_raw` será silencio y la IR no será válida (esperable).

## Referencias

- [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md)
- [`WINDOWING-NORMALIZATION.md`](WINDOWING-NORMALIZATION.md)
- [`SPEC.md`](../SPEC.md) §5
