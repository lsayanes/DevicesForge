# Guía de Captura - DevicesForge

Captura de IR sobre **dispositivos de audio** (mesas analógicas, preamps, procesadores en insert, etc.). Los pasos usan una **consola analógica** como ejemplo habitual; el mismo flujo aplica a cualquier dispositivo que quieras caracterizar.

## 1. Preparación

### Checklist de Equipamiento

- [ ] Dispositivo bajo prueba conectado (ej.: consola de sonido, preamp, cadena de insert)
- [ ] Interface de audio configurada (48kHz / 24-bit)
- [ ] Plugin DevicesForge cargado en DAW
- [ ] Cableado verificado (salida del dispositivo → entrada de la interface)
- [ ] Niveles de referencia calibrados

### Configuración Inicial

```
DAW Session:
  - Sample Rate: 48000 Hz
  - Bit Depth: 24-bit
  - Buffer Size: 512 samples
  
Interface:
  - Input: Punto de captura del dispositivo (ej. direct out de un canal)
  - Output: Para reproducir el sweep hacia el dispositivo
  - Clock: Internal
```

## 2. Captura Básica (IR Plana)

### Paso 1: Configurar el dispositivo

*Ejemplo: canal de consola analógica*

- Ecualizadores en **FLAT** (0 dB)
- Ganancia de canal: **0 dB**
- Pan: **Center**
- Mute/Solo: **Normal**

### Paso 2: Generar sweep
1. Presionar **[GENERATE]** en el plugin
2. Seleccionar "Sine Sweep"
3. Duración: **1.0 segundo**
4. El DAW reproducirá el sweep **a través del dispositivo**

### Paso 3: Capturar
1. Presionar **[CAPTURE]** durante la reproducción
2. El plugin grabará la respuesta
3. Verificar forma de onda en el display

### Paso 4: Verificar
- Respuesta en frecuencia debe ser plana (±0.5 dB)
- No debe haber clipping
- Relación señal/ruido > 60 dB

## 3. Captura Multi-Nivel (Dynamic Convolution)

**Objetivo:** Capturar comportamiento no-lineal del dispositivo (p. ej. saturación de un canal de consola al subir nivel)

### Niveles de Captura

| Nivel | Ganancia | Descripción |
|-------|----------|-------------|
| **Level 1** | -24 dB | Señal baja (sin distorsión) |
| **Level 2** | -12 dB | Señal moderada |
| **Level 3** | 0 dB | Señal nominal |
| **Level 4** | +6 dB | Señal caliente (con saturación) |

### Procedimiento

```
Para cada nivel:

1. Ajustar gain en plugin:
   - Nivel 1: Configurar a -24 dB
   - Nivel 2: Configurar a -12 dB
   - Nivel 3: Configurar a 0 dB
   - Nivel 4: Configurar a +6 dB

2. Presionar [GENERATE]
   - Sweep se reproducirá al nivel seleccionado

3. Presionar [CAPTURE]
   - Plugin guardará IR con metadata del nivel

4. Verificar
   - IR debe mostrar distorsión creciente con nivel (si el dispositivo es no lineal)
   - Verificar que no hay clipping digital
```

### Análisis de Resultados

```
IR @ -24dB:  Lineal, sin distorsión
IR @ -12dB:  Leve saturación en graves (ejemplo consola)
IR @ 0dB:    Respuesta nominal con saturación moderada
IR @ +6dB:   Saturación noticeable, clipping suave
```

## 4. Captura Multi-EQ

**Objetivo:** Capturar diferentes configuraciones de ecualización del dispositivo

### Configuraciones Mínimas

| Preset | Configuración |
|--------|---------------|
| **Flat** | Todos los EQs a 0 dB |
| **Low Cut** | HPF a 80 Hz |
| **High Shelf** | HF shelf +3 dB @ 10 kHz |
| **Mid Boost** | MF bell +4 dB @ 1 kHz |
| **Custom** | Configuración del usuario |

### Procedimiento

```
Para cada preset:

1. Configurar EQ en el dispositivo según preset (ej. sección EQ del canal)
2. Seleccionar preset en plugin
3. Ejecutar captura completa (4 niveles)
4. Guardar set de IRs con metadata del preset

Estructura de archivos:
  IR_Flat_-24dB.wav
  IR_Flat_-12dB.wav
  IR_Flat_0dB.wav
  IR_Flat_+6dB.wav
  
  IR_LowCut_-24dB.wav
  IR_LowCut_-12dB.wav
  ... etc
```

## 5. Verificación

### Checklist de Calidad

- [ ] Respuesta en frecuencia dentro de ±0.5 dB
- [ ] No hay artefactos de ruido
- [ ] Phase response consistente
- [ ] No hay clipping en ningún nivel
- [ ] Dynamic range > 60 dB

### Comparación ABX (Opcional)

1. Reproducir audio procesado con IR
2. Comparar con el **dispositivo original** (misma cadena física)
3. Realizar test ABX ciego
4. Documentar resultados

## 6. Exportación

### Formatos Disponibles

- **WAV 24-bit / 48kHz** - Para convolucionadores
- **WAV 32-bit float** - Máxima precisión
- **Package** - Set completo con metadata

### Metadata Incluida

```json
{
  "device": "Nombre del dispositivo (ej. SSL 4000 G, Neve 1073)",
  "console": "Alias legacy / tipo (ej. consola analógica)",
  "channel": "Punto capturado (ej. Canal 12 direct out)",
  "preset": "Flat",
  "levelDB": 0.0,
  "sampleRate": 48000,
  "irLength": 2048,
  "date": "2026-09-16",
  "notes": "Notas adicionales"
}
```

> En código, `IRMetadata::console` identifica el dispositivo o familia capturada; puede renombrarse a `device` en una versión futura.

## 7. Troubleshooting

### Problema: Clipping en la grabación
**Solución:** Reducir nivel de entrada en la interface o ganancia en el dispositivo bajo prueba

### Problema: Relación S/R baja
**Solución:** Verificar cableado, aumentar nivel de sweep, reducir ruido ambiental

### Problema: IR muy ruidosa
**Solución:** Usar AI Denoise en el plugin, o promediar múltiples capturas

### Problema: Respuesta no plana
**Solución:** Verificar calibración de interface, compensar con EQ de referencia

---

*Guía v1.1 - Septiembre 2026*
