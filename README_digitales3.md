# Técnicas Digitales III – UTN-FRBB
## Laboratorio: Sistema de transmisión digital con filtros (ESP32)

Este proyecto implementa un sistema completo de transmisión y recepción digital con un ESP32, evaluando el efecto de distintos filtros digitales sobre la señal ruidosa.

Hay **tres sketches compactos**:

- `codigo1_adc_threshold_compacto.ino` – **Sin filtro** (threshold directo)
- `codigo2_adc_moving_average_compacto.ino` – **Filtro de promedios móviles**
- `codigo3_adc_median_filter_compacto.ino` – **Filtro de mediana**

En los tres casos se transmite la misma frase:

- `"Aguante Digitales 3"` (19 caracteres)

Y se calcula la BER **por palabra completa**, de modo que cada línea de log corresponde a una instancia de esa frase.

---

## 1. Montaje de hardware

Pines (LilyGo T-Display ESP32):

- **UART de prueba (Serial1)**  
  - TX2: GPIO17  
  - RX2: GPIO16
- **ADC de entrada**  
  - ADC_INPUT_PIN: GPIO34 (ADC1_CH6)
- **Salida digital filtrada**  
  - GPIO_OUT_PIN: GPIO25

Conexiones sugeridas:

1. **Transmisor y ruido analógico**  
   - `TX2 (GPIO17)` va al circuito sumador analógico donde se inyecta ruido desde un generador.
   - La salida del sumador va a `ADC (GPIO34)`.

2. **Salida digital filtrada a receptor UART**  
   - `GPIO25` se conecta directamente a `RX2 (GPIO16)`.

De este modo, la UART TX2 genera la frase ideal, se contamina con ruido en analógico, se muestrea por el ADC, se filtra digitalmente (o no), se vuelve a convertir a 0/1 en GPIO25, y esa señal entra nuevamente al RX2, donde se reconstruyen los bytes recibidos.

---

## 2. Parámetros comunes a los tres códigos

- Frase transmitida: `"Aguante Digitales 3"`  
  - Longitud: 19 bytes.
- Envío UART:
  - Se envía **1 carácter cada 50 ms** (`PATTERN_PERIOD_MS = 50`).
  - En cada palabra completa se envían 19 caracteres.
- Muestreo ADC:
  - `SAMPLING_FREQUENCY_HZ = 20000.0f` → 20 kHz → 1 muestra cada 50 µs.
- Threshold ADC:
  - `ADC_THRESHOLD = 2048` (mitad de 0..4095) para separar 0/1.
- Baudios probados (Serial1):
  - `1200, 2400, 4800, 9600, 9900, 10000, 11000, 19200`  
  - El código cambia automáticamente de baud cada 1 segundo (`BAUD_STEP_INTERVAL_MS = 1000`).

---

## 3. Formato de log (común a los 3 sketches)

Cada línea de log representa **una palabra completa recibida**:

```text
TX:19 RX:19 BitsCmp:152 Err:X BER:Y% BR:BBBB WORD:AAAAAAAAAAAAAAAAAAA
```

- `TX:19`  
  - 19 bytes transmitidos para esta palabra.
- `RX:19`  
  - 19 bytes recibidos y comparados.
- `BitsCmp:152`  
  - 19 bytes × 8 bits = 152 bits comparados.
- `Err:X`  
  - Número de bits que **no coinciden** entre la palabra recibida y `"Aguante Digitales 3"`.
- `BER:Y%`  
  - `Y = Err / 152 × 100` → Bit Error Rate de **esa palabra exacta**.
- `BR:BBBB`  
  - Baud rate actual de Serial1 (ej. 9600, 19200, etc.).
- `WORD:...`  
  - Palabra reconstruida byte a byte a partir de lo recibido por RX2.

Interpretación rápida:

- Si `WORD:Aguante Digitales 3` y `Err:0` → **palabra perfecta, sin errores**.
- Si `WORD` tiene letras cambiadas y `Err>0` → esa palabra llegó con algunos bits corruptos.
- A baudios altos y con ruido fuerte, es común ver `WORD` como caracteres basura y `BER` muy alta.

---

## 4. Código 1: Sin filtro (threshold directo)

Archivo: `codigo1_adc_threshold_compacto.ino`

Camino de señal:

```text
TX2 (GPIO17) --(ruido analógico)--> ADC (GPIO34)
ADC --(comparación con threshold)--> GPIO25
GPIO25 --> RX2 (GPIO16) --> UART RX --> bytes --> BER por palabra
```

Comportamiento esperado:

- A baudios bajos (1200, 2400, 4800) y ruido moderado → BER muy baja o 0%, `WORD` correcta.
- Al subir baudios y/o aumentar el ruido → aparecen errores (
  `Err` pequeño, palabra con 1–2 letras mal).
- A 19200 con ruido alto → BER grande, `WORD` casi irreconocible.

Este código sirve como **referencia base** sin filtrado digital.

---

## 5. Código 2: Filtro de promedios móviles

Archivo: `codigo2_adc_moving_average_compacto.ino`

Camino de señal:

```text
TX2 --> ruido analógico --> ADC --> FILTRO MEDIA MÓVIL --> threshold --> GPIO25 --> RX2
```

Parámetro principal del filtro:

```cpp
// Filtro de promedios moviles: cambiar FILTER_WINDOW_SIZE a 11, 21, 31 o 41 segun el ensayo.
const int FILTER_WINDOW_SIZE = 11;
```

Funcionamiento del filtro:

- Se mantiene un **buffer circular** con las últimas `N` muestras (`FILTER_WINDOW_SIZE`).
- Se mantiene también una **suma acumulada** de esas muestras.
- En cada nueva muestra:
  - Se resta la muestra vieja que sale de la ventana.
  - Se suma la nueva muestra.
  - Se calcula `promedio = suma / N`.
- Ese promedio filtrado se compara con el threshold ADC.

Efectos esperados:

- Reduce el ruido rápido (alta frecuencia).
- Introduce un retardo aproximado de `(N-1)/2` muestras.
- Si `N` es demasiado grande, los bordes de los bits se suavizan demasiado y 
  se puede mezclar información de bits consecutivos.

Ensayos sugeridos (según el enunciado):

- Sin filtro: usar `codigo1_adc_threshold_compacto.ino`.
- Con filtro de promedios móviles con:
  - `N = 11`, `21`, `31`, `41` (cambiar `FILTER_WINDOW_SIZE` y recompilar).
- Para cada N:
  - Variar la amplitud del ruido analógico.
  - Observar cómo cambian `Err` y `BER` a diferentes baudios.
  - Comparar con la BER del caso sin filtro.

---

## 6. Código 3: Filtro de mediana

Archivo: `codigo3_adc_median_filter_compacto.ino`

Camino de señal:

```text
TX2 --> ruido analógico --> ADC --> FILTRO DE MEDIANA --> threshold --> GPIO25 --> RX2
```

Parámetro principal del filtro:

```cpp
// Filtro de mediana: cambiar FILTER_WINDOW_SIZE a 11 o 31 segun el ensayo.
const int FILTER_WINDOW_SIZE = 11;
```

Funcionamiento del filtro de mediana:

- Se guardan las últimas `N` muestras en un buffer.
- En cada nueva muestra:
  - Se actualiza el buffer circular.
  - Se copian las muestras válidas a un array temporal.
  - Se ordena ese array (método simple, suficiente para N chico).
  - La salida del filtro es la **mediana** (valor central del array ordenado).
- Luego, se compara esa mediana con el threshold para decidir 0/1.

Efectos esperados:

- Excelente para eliminar **picos de ruido** (impulsivos).
- Tiende a preservar mejor los bordes de la señal que una media móvil grande.
- Mayor costo computacional que el promedio móvil, pero aceptable para N=11/31.

Ensayos sugeridos:

- Sin filtro: `codigo1_adc_threshold_compacto.ino`.
- Filtro de mediana con:
  - `N = 11` y `N = 31`.
- Comparar:
  - BER vs. `N` y vs. ruido.
  - BER vs. filtro de promedios móviles para la misma ventana.
  - Cómo se ven las palabras en `WORD:` (pérdida de letras, caracteres raros, etc.).

---

## 7. Interpretación de resultados y recomendaciones para el informe

Para el informe final, se sugiere:

1. **Tabla comparativa** por filtro y orden de filtro, por ejemplo:

   | Filtro           | N   | BR (bps) | Nivel de ruido | Err promedio (bits) | BER promedio (%) | Comentarios sobre WORD |
   |------------------|-----|----------|----------------|---------------------|------------------|-------------------------|
   | Sin filtro       | -   | 9600     | Bajo           | 0                   | 0.00             | Palabra perfecta        |
   | Promedio móvil   | 11  | 9600     | Medio          | 2                   | 1.32             | 1 letra mal a veces     |
   | Mediana          | 11  | 9600     | Medio          | 0–1                 | ~0.66            | Mejora picos de ruido   |
   | Promedio móvil   | 41  | 19200    | Alto           | Muy alto            | >30              | Palabra casi ilegible   |
   | Mediana          | 31  | 19200    | Alto           | Alto                | >20              | Mejora algunos picos    |

2. **Capturas de pantalla** del monitor serie:
   - Mostrando ejemplos típicos de cada modo:
     - Sin filtro: `WORD` rota a baudios altos.
     - Con promedio: BER baja pero bordes suavizados.
     - Con mediana: eliminación de picos, mejor lectura en presencia de 
       ruido impulsivo.

3. **Conclusiones técnicas**:
   - Ventajas y desventajas del filtro de promedios móviles vs. mediana.
   - Influencia del tamaño de ventana `N`.
   - Influencia de la frecuencia de muestreo (si se hacen ensayos variando `SAMPLING_FREQUENCY_HZ`).

Con estas tres variantes de código y el mismo formato de logs, es posible
comparar de forma clara el desempeño del sistema de transmisión digital
sin filtro, con filtro de promedios móviles y con filtro de mediana,
frente a distintos niveles de ruido analógico y diferentes baudios.
