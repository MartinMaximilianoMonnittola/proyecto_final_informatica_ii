# Proyecto final Informática 2 — Brazo ESP32, Qt y clasificación por humedad y temperatura

Versión extendida a partir del **proyecto corregido**: conserva los seis servomotores, el PWM calibrable del firmware, la interfaz manual, la comunicación serie, las secuencias anteriores y la telemetría DHT22. Se añade una pestaña **CICLO AUTOMÁTICO / DHT22**, exportación/importación del ciclo completo y repeticiones finitas configurables.

## Contenido

```
Proyecto_Final_Informatica_2_AUTO/
├── README.md
├── CAMBIOS.txt
├── ejemplo_ciclo_vacio.json
├── firmware/BrazoArduino/
│   ├── BrazoArduino.ino
│   └── protocolo.h
├── qt/
│   ├── Proyecto_Final_Informatica_2.pro
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── mainwindow.h / mainwindow.cpp
│   └── jointbar.h / jointbar.cpp
└── tests/test_protocolo.cpp
```

## Qué hace el ciclo automático

1. **Agarrar**: reproduce en orden una o varias posiciones de los seis servomotores. Por ejemplo, acercar, bajar, cerrar suavemente la pinza. La última postura es la que debe dejar el brazo sujetando el objeto.
2. **Medir**: opcionalmente reproduce otras posiciones (para ubicar la garra cerca del objeto); después permanece **10 segundos** en la última postura ordenada. En esos 10 segundos considera **solo las tramas DHT22 recibidas dentro de la ventana**, calcula la humedad relativa promedio y requiere al menos **2 lecturas válidas nuevas**.
3. **Ruta fuera de límites ambientales**: si la HR promedio es menor que el mínimo o mayor que el máximo **O** la temperatura promedio está fuera de su propio rango, reproduce la etapa 3.
4. **Ruta dentro de límites ambientales**: únicamente si la HR promedio y la temperatura promedio están **ambas dentro** de sus respectivos rangos (extremos incluidos), reproduce la etapa 4.

Se toman pares de temperatura y humedad del DHT22 durante los mismos **10 segundos**, con al menos **dos pares de lecturas nuevos y válidos**. Los rangos iniciales son **40 a 70 % HR** y **15,0 a 30,0 °C**. Se pueden modificar desde Qt y ambos controles permanecen activos. Los límites son para la demostración y **no representan una recomendación oficial de almacenamiento**. El DHT22 mide condiciones ambientales, **no la humedad interna ni superficial del objeto**.

**Seguridad mecánica:** no hay sensor de fuerza, detección de choque ni confirmación física de que el objeto quedó agarrado. Un servo trabado puede quemarse durante la espera de 10 s; calibrar la pinza para sostener sin forzar topes. El comando `STOP` congela la consigna, **pero no desconecta el torque**. La fuente para servos 5–6 V es externa, dimensionada para seis servos, con GND común. No conectar servos a 3V3 o USB.

## Pines ESP32 clásico

| Componente | GPIO | Alimentación |
|---|---:|---|
| S0 pinza MG90S | 12 | fuente 5–6 V externa |
| S1 muñeca 1 MG90S | 14 | fuente 5–6 V externa |
| S2 muñeca 2 MG90S | 27 | fuente 5–6 V externa |
| S3 codo MG996R | 26 | fuente 5–6 V externa |
| S4 hombro MG996R | 25 | fuente 5–6 V externa |
| S5 base MG996R | 33 | fuente 5–6 V externa |
| DHT22 DATA | 32 | 3V3, GND |
| Botón de parada opcional | 23 | a GND, INPUT_PULLUP |

Para el DHT22 **de 4 patas**, resistencia pull-up de ~4.7–10 kΩ entre DATA y 3V3, salvo que un módulo de 3 pines ya la incorpore. La unión GND es indispensable. Mantener el sensor fuera del recorrido de la garra y protegido de presiones.

## Instalar / ejecutar

### Firmware en Arduino IDE

1. Instalar soporte para placas ESP32.
2. Instalar **ESP32Servo**, **DHT sensor library by Adafruit** y **Adafruit Unified Sensor**.
3. Abrir `firmware/BrazoArduino/BrazoArduino.ino` y elegir placa y puerto COM.
4. Pulsar **Subir**. Cerrar Qt antes de flashear para liberar el COM.

**Nota**: El firmware es el mismo de la última entrega corregida; si ese sketch ya está cargado y el DHT22 transmite `DHT,temperatura,humedad` a 115200, no es necesario cambiarlo para usar las nuevas rutas. La automatización se implementa en la aplicación Qt.

### Interfaz en Qt Creator

1. Abrir `qt/Proyecto_Final_Informatica_2.pro` (qmake) o `qt/CMakeLists.txt` (CMake).
2. Tener instalados Qt **Widgets** y **SerialPort** del mismo kit Qt. C++17.
3. En Qt Creator ejecutar **Limpiar todo** y luego **Recompilar todo**.
4. Conectar ESP32 por COM, a 115200 baudios, en modo **Info2: SET / MOVE**.
5. Abrir **CICLO AUTOMÁTICO / DHT22**.

### Programar movimientos sin escribir código

1. En **CONTROL MANUAL / SECUENCIAS**, mover los seis controles a la primera posición de recogida. Cambiar a **CICLO AUTOMÁTICO** y pulsar `+ GUARDAR POSE ACTUAL` de la etapa **1**.
2. Volver al control manual, ajustar a la siguiente posición y guardarla de nuevo en la etapa 1. Repetir (la última postura debe cerrar la pinza **sin atascar** el MG90S).
3. En etapa **2**, opcionalmente guardar una o varias posturas de acercamiento al sensor. Si queda vacía, la medición se hará desde la postura final de la etapa 1.
4. Grabar las posturas de destino para etapa **3**, destinada a **humedad O temperatura fuera de su respectivo rango**, y etapa **4**, destinada a **ambas variables dentro de sus respectivos rangos**. Debe existir por lo menos una pose en cada ruta para iniciar un ciclo completo.
5. Elegir el `TIEMPO POR PASO (ms)` desde el panel manual. En la pestaña automática configurar **HR mínima (40 %) y máxima (70 %)**, además de **temperatura mínima (15 °C) y máxima (30 °C)**. Todos son editables; el sistema mantiene mínima ≤ máxima. Guardar con **GUARDAR LAS 4 ETAPAS EN JSON** para recuperarlo después.
6. Comprobar trayectorias sin carga con **PROBAR MOVIMIENTO** en cada etapa antes de automatizar.
7. En la pestaña automática pulsar **DEFINIR BUCLES** para elegir de **1 a 1000** vueltas completas (valor inicial: **1**). Una vuelta ejecuta AGARRAR → posiciones opcionales de MEDIR → **una nueva medición de 10 s** → ruta 3 ó 4 según la nueva lectura. Si se configuran 3 bucles, se repite tres veces la cadena completa, no solamente el último movimiento. La pantalla informa qué bucle está en curso.
8. Pulsar **EXPORTAR CICLO COMPLETO (JSON)** para crear `ciclo_completo.json` con todas las poses de las cuatro etapas, duración por paso, rangos de humedad y temperatura, y cantidad de bucles. Al lado aparece **IMPORTAR CICLO COMPLETO (JSON)**: permite seleccionar ese archivo y recupera automáticamente **todos** esos valores. Si ya hay posturas configuradas, solicita confirmación antes de reemplazarlas. Importar **no mueve físicamente el brazo**, solo prepara el ciclo en Qt. **GUARDAR LAS 4 ETAPAS EN JSON** sigue disponible y guarda el mismo perfil compatible.

   **Para recuperar un ciclo:** abrir la pestaña `CICLO AUTOMATICO / DHT22` → **IMPORTAR CICLO COMPLETO (JSON)** → seleccionar el archivo `.json` previamente exportado → comprobar las cuatro listas, los rangos de HR y temperatura, los bucles y el tiempo por paso en control manual → probar con el brazo libre antes de iniciar.
9. Pulsar **INICIAR CICLO COMPLETO**. También se ofrece **MEDIR SOLO 10 SEGUNDOS** para pruebas sin mover el brazo.

**IMPORTANTE si hay más de un bucle:** programar las etapas 3 y 4 para **liberar el objeto** y devolver la pinza a una postura adecuada para agarrar la siguiente pieza. El algoritmo no detecta físicamente si el objeto se soltó o si hay otro esperando. Se pide confirmación antes de comenzar con varias repeticiones. Entre vueltas hay una pausa cancelable de 800 ms. La parada cancela también la próxima vuelta.

### Condiciones de abortado / seguridad del algoritmo

- `DHT,ERR`, trama inválida o valores fuera de rango durante la medición: **abortado**; no se seleccionan rutas.
- Menos de dos pares nuevos válidos (temperatura + humedad) en 10 s: **sin clasificación**, no se selecciona ruta.
- Desconexión de COM, errores de órdenes enviados al ESP32 o botón de parada físico: **abortado**.
- **DETENER CICLO** o **Ctrl+Espacio**: envía `STOP` y cancela los temporizadores, incluso el de espera entre bucles. No elimina el torque eléctrico.
- Se bloquean los controles manuales durante una automatización para impedir órdenes concurrentes.
- La etapa 2 mantiene la **consigna** del servo; el sistema no posee sensor de presencia ni realimentación de posición.

## Archivos de movimientos

Tanto `ciclo_automatico.json` como `ciclo_completo.json` usan el mismo formato de ciclo, que se puede guardar, exportar e importar:

- Los pasos de `pick`, `measure_position`, `humid` y `dry` (listas de arreglos de 6 ángulos).
- El tiempo por paso en milisegundos.
- Versión 2 del JSON: `humidity_min_percent` y `humidity_max_percent`, límites inclusivos entre 0 y 100 % (por defecto 40 y 70).
- `temperature_min_c` y `temperature_max_c`: límites inclusivos en °C (-40,0 a 80,0 °C, por defecto 15 y 30).
- **Retrocompatibilidad:** los perfiles JSON **versión 1** (con el antiguo `humidity_threshold_percent`) se pueden importar para **recuperar las cuatro secuencias, la duración y los bucles**; sus criterios antiguos son reemplazados por los nuevos rangos predeterminados **40–70 % HR y 15–30 °C**. Se informa de la conversión en la pantalla. Se recomienda guardar nuevamente en la versión 2.
- `repeat_count`: cuántas veces ejecutar el **ciclo completo** (1 a 1000, por defecto 1); los perfiles antiguos sin este campo se cargan como 1 bucle.

**Las secuencias anteriores** de `GUARDAR / EXPORTAR POSICIONES` se siguen guardando con su **formato JSON anterior**. No se mezclan con el nuevo perfil de ciclo automático.

`ejemplo_ciclo_vacio.json` es una plantilla válida **sin movimientos pregrabados**. Se evita suministrar ángulos de ejemplo que puedan hacer chocar un brazo real. Primero hay que programar posturas seguras para la estructura mecánica.

## Estado de pruebas

- Se ejecutaron las pruebas nativas del analizador del protocolo con `g++`.
- Se verificó el empaquetado y la consistencia estática de los archivos de Qt.
- **No se compiló Qt en este entorno** (no estaban disponibles las bibliotecas de desarrollo Qt), ni se hizo prueba con el brazo real. Compilar y verificar en la PC, primero sin carga, es imprescindible.


## Diagnóstico serial (versión revisada)

Esta revisión agrega **Ctrl+L** en Qt para inspeccionar las tramas enviadas (**TX**) y recibidas (**RX**) sin cerrar la aplicación. Después de conectar, espera 4 segundos y mueve suavemente un eje sin carga. Abre **Ctrl+L** y pulsa **ACTUALIZAR REGISTRO**. El mensaje `ERR,TRAMA_INVALIDA` mostrará también el último comando enviado. Se espera `PING`, `GET`, `PONG`, `POS,...`, `DHT,<temp>,<hum>` y `SET,<indice>,<angulo>` con respuesta `OK,SET`. El registro conserva las últimas 300 líneas en memoria durante la ejecución; no envía datos a Internet.

**Hallazgo:** las fuentes de Qt y firmware incluidas en este ZIP **ya utilizan el mismo protocolo**. Si en Arduino IDE se ven las lecturas DHT y Qt sigue sin verlas, conviene recompilar el proyecto Qt completo, asegurarse de ejecutar el binario nuevo y consultar Ctrl+L. Cerrar el Monitor Serie de Arduino antes de abrir el puerto desde Qt. No se ha demostrado que sea necesario cambiar el formato de los comandos.

## Modificación: doble rango ambiental configurable (17-11-2026)

La decisión usa los **promedios** de humedad y temperatura medidos durante 10 segundos (mínimo dos pares de tramas DHT22). El indicador ambiental del panel de control usa la **lectura instantánea**.

- **Ruta 4 (DENTRO):** `40 % <= humedad <= 70 %` **Y** `15 °C <= temperatura <= 30 °C`.
- **Ruta 3 (FUERA):** humedad **o** temperatura fuera de su respectivo intervalo. Basta con que **una sola variable** esté fuera de rango.
- Los **cuatro límites son editables** en Qt. Los extremos cuentan como dentro de los límites.

| HR media | T media | Clasificación |
|---:|---:|---|
| 40 % | 15 °C | **Ruta 4: dentro** (extremos incluidos) |
| 70 % | 30 °C | **Ruta 4: dentro** (extremos incluidos) |
| 55 % | 22 °C | **Ruta 4: dentro** |
| 35 % | 22 °C | **Ruta 3: fuera** (humedad demasiado baja) |
| 75 % | 22 °C | **Ruta 3: fuera** (humedad demasiado alta) |
| 55 % | 12 °C | **Ruta 3: fuera** (temperatura demasiado baja) |
| 55 % | 32 °C | **Ruta 3: fuera** (temperatura demasiado alta) |

**Archivos JSON:** se exporta la versión 2 con los cuatro límites. Los ciclos antiguos de versión 1 se pueden importar y conservan las posturas, pero la clasificación antigua se sustituye por los rangos nuevos predeterminados, para no dejar desactivada la temperatura.

El firmware ESP32 no necesita cambios si ya transmite `DHT,<temperatura>,<humedad>`. Se pueden correr las pruebas de clasificación sin Qt compilando `tests/test_ambient_classification.cpp` con un compilador C++17. **No se ha probado físicamente el brazo**.


## Revisión HR+T v3: evitar ejecutar un binario antiguo

La ventana tiene el distintivo **RANGOS HR+T v3**, visible en el titulo y en el
panel de ciclo. Si aparece `control de temperatura desactivado` o `HR limite 60 %`
en los resultados, la aplicacion que esta corriendo es una **version anterior**.
Consultar `LEER_PRIMERO_VERSION_v3.txt`, crear una carpeta de extraccion nueva,
limpiar/recompilar Qt, ejecutar el nuevo binario y recuperar las poses via JSON.

El caso real reportado **20,5 % de HR y 46,5 °C** se comprobo en las pruebas
unitarias: el resultado correcto es la **RUTA 3**, fuera de rango. Tras cada
medicion el panel muestra la ruta y el motivo concreto.
