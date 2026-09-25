# Guía de Captura - DevicesForge

Captura de IR sobre **dispositivos de audio** (mesas analógicas, preamps, procesadores en insert, etc.). Los pasos usan una **consola analógica** como ejemplo habitual; el mismo flujo aplica a cualquier dispositivo que quieras caracterizar.

## Estéreo del plugin vs IR mono

El VST declara **Stereo In / Stereo Out**. Eso es el bus del host, no “el dispositivo es estéreo” ni “se capturan dos IRs”.

Hoy el pipeline es **un camino, una IR**:

| Etapa | Qué hace |
|-------|----------|
| Sweep | El **mismo** tono se copia a L y a R. No hace falta cablear las dos salidas físicas: con una basta. |
| Captura | L y R sí llegan por separado al plugin, pero se mezclan: `IR = 0.5 × (L + R)`. |
| Emulación | Esa IR **única** se aplica a cada canal de salida. L y R suenan igual (misma coloración). |

Un loop tipo “placa → dos canales de mixer paneados L/R → Stereo In de la placa” **no** produce un IR por canal. Produce **el promedio** de ambos; las diferencias entre lados (EQ, gain, paneo, crosstalk) se pierden. Al reproducir no hay imagen estéreo de la mixer: hay el mismo clon a izquierda y derecha.

Si solo hay señal en un canal y el otro llega en silencio, ese promedio **atenúa 6 dB** la toma buena. InPeak igual se mueve (mira el pico de cualquier canal), pero la IR queda más floja.

Para pedal o un canal de consola este modelo es el correcto. Captura dual (una IR por canal) no existe todavía; el detalle interno está en [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md).

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

### Paso 2: Calibrar el loop (antes de Generate)

**Cal** manda señal **sin capturar** ni tocar la IR. Sirve para poner Gain, faders de la mixer y la entrada de la placa donde querés, mirando **InPeak**.

1. Cablear el retorno: salida del plugin → dispositivo → entrada del plugin (vía interface). **Monitor ON** en Cubase.
2. **CalSig** = **1kHz** (tono continuo, el más claro para nivel) o **Sweep** (repite el barrido de **Duration**, para oír graves/agudos del camino).
3. Activar **Cal**. Ajustá **Gain** y los niveles del hardware hasta que InPeak quede entre **−18 y −6 dBFS**, sin recorte.
4. Apagar **Cal** (o dejarlo: si disparás Generate, Cal se apaga solo).

Cal no escribe `latest/` ni cambia IRLen. El tono usa el mismo Gain que el sweep de captura, así el nivel que ves es el de la toma.

### Paso 3: Generar sweep (y captura automática)
1. Seleccionar **Signal** = Sine Sweep y **Duration** = **1.0 s**
2. Cablear el retorno: salida del plugin → dispositivo → entrada del plugin (vía interface)
3. Activar **Generate** (Off → On). El plugin:
   - reproduce el sweep hacia el dispositivo;
   - dispara la captura con **100 ms de pre-trigger** y post = duración + cola (ver [`CAPTURE-RING-BUFFER.md`](CAPTURE-RING-BUFFER.md)).
4. Para repetir, poner Generate en Off y volver a On.

### Paso 4: Capturar (UI futura)
En la versión actual **no** hace falta un botón **[CAPTURE]** aparte: la ventana pre/post se arma sola al activar Generate. Un control Capture manual y visualización de forma de onda llegarán con el editor gráfico (Fase 4).

### Paso 5: Verificar
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
