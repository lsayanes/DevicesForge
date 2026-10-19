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

En Cubase, **Stereo In (Izquierda)** vs **(Derecha)** es la entrada física de la pista. Si el pedal está en la derecha y la pista queda en (Izquierda), InPeak no se mueve y Generate graba silencio.

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

### Paso 2: Nivel de envío

**Send** es el volumen del sweep hacia el pedal/consola (−12…+12 dB). **Gain** no entra en la toma: solo el volumen de la emulación después.

1. Cablear el retorno: salida del plugin → dispositivo → entrada del plugin. **Monitor ON** en Cubase.
2. Empezar **Send** en 0 dB. Si la primera toma recorta (`quality=clipping` o acople), bajar Send y/o el Level del pedal. Si InPeak queda muy bajo, subir Send.
3. **Mix** no afecta la captura.

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

### Paso 6: Guardar y recargar la IR

La IR vive en el plugin y también en el estado del **proyecto** (si guardás el .cpr). Si **sacás el insert** y lo volvés a poner, Cubase crea una instancia nueva: la muestra en memoria se pierde.

- **SaveIR**: diálogo para elegir carpeta y nombre. Escribe un WAV float 32-bit. Por defecto ofrece `~/Documents/DevicesForge/library/`.
- **LoadIR**: diálogo para ir a buscar ese archivo, un WAV PCM 16/24 o float, o un `.dfcapture`. Si la tasa difiere de la del proyecto, se remuestrea.
- **IRLen > 0** confirma que hay IR cargada.

`exports/latest/` sigue siendo la copia automática de la última toma; SaveIR es la copia que **vos** archivás.

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
**Solución:** Promediar varias capturas y subir el nivel del barrido sin llegar al clip

### Problema: Respuesta no plana
**Solución:** Verificar calibración de interface, compensar con EQ de referencia

## 8. Capturador offline

Sin plugin. Por comando es `dfcapture` (`build/bin/dfcapture`). Durante el desarrollo: `./build.sh` y después `./run.sh` (abre `build/Capturador.app`). Cuando la app esté estable, `./package.sh` la instala en Aplicaciones. Duración y repeticiones se eligen una vez y valen para generar y para analizar.

```text
dfcapture make-sweep --rate 48000 --duration 5 --repeats 3 -o sweep.wav
dfcapture analyze grabacion.wav -o equipo.dfcapture --name "..." --author "..."
```

`analyze` tiene que repetir el mismo `--duration` y `--repeats`. El sample rate lo lee del WAV. El archivo de barrido es: silencio, marca, N barridos separados por 0,5 s, marca, silencio. Esa cola de 0,5 s es el largo máximo de la IR; una cola más larga se mezcla con el barrido siguiente.

Si la cinta corrió más rápido o más lento, la distancia entre las dos marcas no coincide con la esperada y la grabación se remuestrea antes de deconvolucionar. Eso corrige la **velocidad media**. El wow y el flutter (la variación rápida) no se corrigen: borronean los agudos. El reporte `equipo.quality.txt` lo deja anotado en `limit=`.

Las pasadas no se suman como IR: con wow cada una llega corrida distinto y la suma cancela bandas (en la Revox A77 de prueba, −6 dB en 1,3–2,5 kHz). Se promedia la **magnitud** (|H|²) de las pasadas y la IR se arma de **fase mínima** (cepstro real). La respuesta en frecuencia es la de la cinta; el pico queda al inicio y positivo, así el Mix del plugin no hace peine aunque el equipo invierta la polaridad (`polarity=` en el reporte). Lo que no se guarda es el corrimiento temporal de cada pasada, que en una cinta es mayormente wow.

---

*Guía v1.1 - Septiembre 2026*
