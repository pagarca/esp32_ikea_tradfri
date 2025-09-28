# Zigbee Coordinator (ESP32-C6) — IKEA TRÅDFRI detection, LED + buzzer

This app turns your ESP32‑C6 into a Zigbee Coordinator that:

- Forms its own Zigbee network (BDB network formation) and opens it for joining (steering).
- Detects newly joined devices and reads ZCL Basic cluster (0x0000) attributes “Manufacturer Name” (0x0004) and “Model Identifier” (0x0005).
- If it detects manufacturer containing “IKEA” and/or model containing “TRÅDFRI”, it raises an ALERT in the logs.
- Visual and audible feedback: when an IKEA TRÅDFRI bulb is detected, the on‑board RGB LED turns red and an active buzzer on GPIO10 beeps at 2 Hz for 15 seconds; then the LED returns to green and the buzzer stops.

The code and configuration are kept simple and self‑contained.

## Requirements

- ESP‑IDF 5.5.1 (or compatible) with ESP32‑C6 support.
- Use the ESP‑IDF PowerShell on Windows so that `idf.py` is on PATH.
- Serial connection to the device (for example, COM6).

## Hardware assumptions

- An on‑board addressable RGB LED (WS2812/NeoPixel) on GPIO 8 (ESP32‑C6 DevKitC default). Colors are sent in GRB order; if you see swapped colors, see Customization.
- An active buzzer connected to GPIO 10 (logic HIGH = ON). The app blinks it at 2 Hz during alerts.

You can change these pins in code via the defines `BOARD_RGB_LED_GPIO` and `BUZZER_GPIO` in `main/main.c`.

## Key files

- `main/main.c`: Coordinator logic, device discovery, Basic attribute reads, LED and buzzer control.
- `main/CMakeLists.txt`: declares the main component and its dependencies.
- `main/idf_component.yml`: uses managed Zigbee components `espressif/esp-zigbee-lib` and `espressif/esp-zboss-lib`.
- `partitions.csv`: partition table with `zb_storage` / `zb_fct` for Zigbee persistence.
- `sdkconfig.defaults`: target `esp32c6`, Zigbee enabled, ZC/ZR role, native IEEE 802.15.4 radio, and custom partition table.

## Build and flash (Windows — ESP‑IDF PowerShell)

Open “ESP‑IDF PowerShell”, navigate to the project folder, and run:

```powershell
idf.py set-target esp32c6
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py -p COM6 flash monitor
```

Replace `COM6` with your serial port. `reconfigure` applies `sdkconfig.defaults` (Coordinator role, etc.).

## Pair the IKEA TRÅDFRI bulb

1. Put the bulb in pairing mode (check your TRÅDFRI model reset procedure; typically power‑cycle several times until it blinks).
2. The Coordinator automatically opens the network after forming it (BDB steering). If the joining window closes, reset the board to reopen it. The app also periodically reopens steering.

## What you’ll see in the monitor

```text
I (xxx) ZB_SCAN: ZDO signal: ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP (...), status: ESP_OK
I (xxx) ZB_SCAN: Forming network (BDB network formation)...
I (xxx) ZB_SCAN: Network formed. Opening for joining (steering 180s)...
I (xxx) ZB_SCAN: DEVICE_ANNCE: short=0xABCD ieee=... cap=0x..
I (xxx) ZB_SCAN: SimpleDesc: ep=1 profile=0x0104 device=0x0100
I (xxx) ZB_SCAN: Reading Basic attrs (tsn=..) to 0xABCD/ep1
I (xxx) ZB_SCAN: Basic attr 0x0004='IKEA of Sweden' (src 0xABCD ep1)
I (xxx) ZB_SCAN: Basic attr 0x0005='TRADFRI bulb E27 ...' (src 0xABCD ep1)
W (xxx) ZB_SCAN: ALERT: IKEA TRÅDFRI bulb detected (0xABCD ep1)
```

When the ALERT triggers:

- The RGB LED turns red for 15 seconds and then returns to green (idle).
- The buzzer on GPIO10 beeps at 2 Hz (on/off every 250 ms) during those 15 seconds, then stops.

## Customization

 Alert duration: `ALERT_DURATION_MS` in `main/main.c` (default 15000 ms)
- Buzzer volume: `BUZZER_VOLUME_PCT` (0–100) in `main/main.c` (uses LEDC PWM)

## Troubleshooting

# Zigbee Coordinator (ESP32‑C6) — IKEA TRÅDFRI detection, LED + buzzer
- idf.py not found / build errors in regular PowerShell:
   - Use the “ESP‑IDF PowerShell” so the environment is properly configured.
- Build fails with Green Power or Zigbee link errors:
   - The steering window is ~180 s after network formation. Reset the board or wait for the periodic reopen.
- You see DEVICE_ANNCE but no ALERT:
   - The app queries endpoints and reads Basic on HA profile endpoints; watch the log for Read Attribute responses. Some devices can be slow to respond.
   - For an active buzzer, adjust the blink frequency (timer period). For a passive buzzer, consider using PWM (LEDC) with an audible frequency instead of on/off toggling.

## License

---

## Español

- Indicadores visual y acústico: cuando se detecta una bombilla IKEA TRÅDFRI, el LED RGB integrado se pone en rojo y un zumbador activo en el GPIO10 parpadea a 2 Hz durante 15 segundos; después el LED vuelve a verde y el zumbador se apaga.

El código y la configuración son simples y autocontenidos.

### Requisitos

- ESP‑IDF 5.5.1 (o compatible) con soporte para ESP32‑C6.
- Usar la “ESP‑IDF PowerShell” en Windows para tener `idf.py` en el PATH.
- Conexión serie al dispositivo (por ejemplo, COM6).

### Suposiciones de hardware

- LED RGB direccionable (WS2812/NeoPixel) en el GPIO 8 (por defecto en ESP32‑C6 DevKitC). Los colores se envían en orden GRB; si ves colores cambiados, revisa Personalización.
- Zumbador activo conectado al GPIO 10 (nivel alto = encendido). La app lo hace parpadear a 2 Hz durante las alertas.

Puedes cambiar estos pines en el código mediante `BOARD_RGB_LED_GPIO` y `BUZZER_GPIO` en `main/main.c`.

### Archivos clave

- `main/main.c`: lógica de Coordinador, descubrimiento, lecturas Basic, control de LED y zumbador.
- `main/CMakeLists.txt`: declara el componente principal y dependencias.
- `main/idf_component.yml`: usa componentes Zigbee gestionados `espressif/esp-zigbee-lib` y `espressif/esp-zboss-lib`.
- `partitions.csv`: tabla de particiones con `zb_storage` / `zb_fct` para persistencia.
- `sdkconfig.defaults`: objetivo `esp32c6`, Zigbee activado, rol ZC/ZR, radio IEEE 802.15.4 nativa y tabla de particiones personalizada.

### Compilar y flashear (Windows — ESP‑IDF PowerShell)

Abre la “ESP‑IDF PowerShell”, navega a la carpeta del proyecto y ejecuta:

```powershell
idf.py set-target esp32c6
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py -p COM6 flash monitor
```
1. Pon la bombilla en modo emparejamiento (consulta el método de reset de tu modelo TRÅDFRI; típicamente ciclos rápidos de encendido/apagado hasta que parpadea).
2. El Coordinador abre la red automáticamente tras formarla (steering BDB). Si se cierra la ventana de emparejamiento, reinicia la placa para reabrirla. La app también reabre el steering periódicamente.

I (xxx) ZB_SCAN: DEVICE_ANNCE: short=0xABCD ieee=... cap=0x..
I (xxx) ZB_SCAN: SimpleDesc: ep=1 profile=0x0104 device=0x0100
I (xxx) ZB_SCAN: Leyendo Basic attrs (tsn=..) a 0xABCD/ep1
I (xxx) ZB_SCAN: Basic attr 0x0004='IKEA of Sweden' (src 0xABCD ep1)
I (xxx) ZB_SCAN: Basic attr 0x0005='TRADFRI bulb E27 ...' (src 0xABCD ep1)
```

Cuando se dispare la ALERTA:

- El LED RGB se pone rojo durante 15 segundos y luego vuelve a verde (reposo).

- Canales Zigbee: `ZB_SCAN_CHANNEL_MASK` (por defecto 11–26).
- Pines del LED y zumbador: cambia `BOARD_RGB_LED_GPIO` (por defecto 8) y `BUZZER_GPIO` (por defecto 10) en `main/main.c`.

### Solución de problemas

- El build falla con símbolos de Green Power o enlaces Zigbee:
   - Asegúrate de que el rol sea ZC/ZR (`CONFIG_ZB_ZCZR=y`) y que Green Power esté deshabilitado (por defecto en este proyecto). Ejecuta `idf.py reconfigure`.
- La bombilla no se une:
   - Verifica que está en modo emparejamiento.
   - La ventana de “steering” dura ~180 s tras formar la red. Reinicia la placa o espera a la reapertura periódica.
   - Para zumbador activo, ajusta la frecuencia de parpadeo (periodo del timer). Para zumbador pasivo, considera usar PWM (LEDC) con una frecuencia audible en lugar de parpadeo ON/OFF.

### Licencia

Ejemplo didáctico basado en ESP‑IDF y las librerías Zigbee de Espressif.

# Zigbee Coordinator (ESP32-C6) — IKEA TRÅDFRI detection, LED + buzzer

This app turns your ESP32‑C6 into a Zigbee Coordinator that:

- Forms its own Zigbee network (BDB network formation) and opens it for joining (steering).
- Detects newly joined devices and reads ZCL Basic cluster (0x0000) attributes “Manufacturer Name” (0x0004) and “Model Identifier” (0x0005).
- If it detects manufacturer containing “IKEA” and/or model containing “TRÅDFRI”, it raises an ALERT in the logs.
- Visual/audible feedback: while an IKEA TRÅDFRI bulb is detected, the on‑board RGB LED turns red and an active buzzer on GPIO10 beeps at 2 Hz for 15 seconds; then the LED returns to green and the buzzer stops.

The code and configuration are kept simple and self‑contained.

## Requirements

- ESP‑IDF 5.5.1 (or compatible) with ESP32‑C6 support.
- Use the ESP‑IDF PowerShell on Windows so that `idf.py` is on PATH.
- Serial connection to the device (e.g. COM6).

## Hardware assumptions

- An on‑board addressable RGB LED (WS2812/NeoPixel) on GPIO 8 (ESP32‑C6 DevKitC default). Colors are mapped with GRB order in code; if you see swapped colors, see Customization.
- An active buzzer connected to GPIO 10 (logic HIGH = ON). The app drives it in a 2 Hz on/off pattern during alerts.

You can change these pins in code via the defines `BOARD_RGB_LED_GPIO` and `BUZZER_GPIO` in `main/main.c`.

## Key files

- `main/main.c`: Coordinator logic, device discovery, Basic attribute reads, LED and buzzer control.
- `main/CMakeLists.txt`: declares the main component and its dependencies.
- `main/idf_component.yml`: uses managed Zigbee components `espressif/esp-zigbee-lib` and `espressif/esp-zboss-lib`.
- `partitions.csv`: partition table with `zb_storage` / `zb_fct` for Zigbee persistence.

```powershell
idf.py set-target esp32c6
Replace `COM6` with your serial port. `reconfigure` applies `sdkconfig.defaults` (Coordinator role, etc.).

## Pair the IKEA TRÅDFRI bulb

1. Put the bulb in pairing mode (check your TRÅDFRI model reset procedure; typically power‑cycle several times until it blinks).
2. The Coordinator automatically opens the network after forming it (BDB steering). If the joining window closes, just reset the board to reopen it. The app also reopens steering periodically.

## What you’ll see in the monitor

```text
I (xxx) ZB_SCAN: ZDO signal: ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP (...), status: ESP_OK
I (xxx) ZB_SCAN: Forming network (BDB network formation)...
I (xxx) ZB_SCAN: Network formed. Opening for joining (steering 180s)...
I (xxx) ZB_SCAN: DEVICE_ANNCE: short=0xABCD ieee=... cap=0x..
I (xxx) ZB_SCAN: SimpleDesc: ep=1 profile=0x0104 device=0x0100
I (xxx) ZB_SCAN: Reading Basic attrs (tsn=..) to 0xABCD/ep1
I (xxx) ZB_SCAN: Basic attr 0x0004='IKEA of Sweden' (src 0xABCD ep1)
I (xxx) ZB_SCAN: Basic attr 0x0005='TRADFRI bulb E27 ...' (src 0xABCD ep1)
W (xxx) ZB_SCAN: ALERT: IKEA TRÅDFRI bulb detected (0xABCD ep1)
```

When the ALERT triggers:
- The RGB LED turns red for 15 seconds and then returns to green (idle).
- The buzzer on GPIO10 beeps at 2 Hz (on/off every 250 ms) during those 15 seconds, then stops.

## Customization

- Zigbee channels: `ZB_SCAN_CHANNEL_MASK` (defaults to 11–26).
- LED and buzzer pins: change `BOARD_RGB_LED_GPIO` (default 8) and `BUZZER_GPIO` (default 10) in `main/main.c`.
- LED color order: the code uses GRB when sending to the LED strip. If colors look swapped, invert the channel order in `led_set_rgb()`.
- Buzzer blink rate: set in `buzzer_timer_create()` via the FreeRTOS timer period (default 250 ms → 2 Hz). Increase/decrease to change the beep cadence.
- Main task stack size: `CONFIG_MAIN_TASK_STACK_SIZE` in `sdkconfig.defaults`.

## Troubleshooting

- idf.py not found / build errors in regular PowerShell:
   - Use the “ESP‑IDF PowerShell” so the environment is properly configured.

- Build fails with Green Power or Zigbee link errors:
   - Ensure ZC/ZR role is enabled (`CONFIG_ZB_ZCZR=y`) and Green Power is disabled (default in this project). Run `idf.py reconfigure` to apply defaults.

- Bulb does not join:
   - Verify the bulb is in pairing mode.
   - The steering window is ~180 s after network formation. Reset the board or wait for the periodic reopen.

- You see DEVICE_ANNCE but no ALERT:
   - The app queries all endpoints and reads Basic on HA profile endpoints; watch the log for Read Attribute responses. Some devices can be slow to respond.

- LED shows wrong colors:
   - Adjust the channel order in `led_set_rgb()` (e.g., swap R and G).

- Buzzer too loud / not loud enough:
   - For an active buzzer, adjust the blink frequency (timer period). For a passive buzzer, consider using PWM (LEDC) with an audible frequency instead of on/off toggling.

## License

Educational example using ESP‑IDF and Espressif Zigbee libraries.

# Coordinador Zigbee (ESP32-C6) – Detección de IKEA TRÅDFRI

Esta app convierte tu ESP32‑C6 en un Coordinador Zigbee que:

- Forma su propia red (BDB network formation) y la abre al emparejamiento (steering).
- Detecta nuevos dispositivos que se unan y lee los atributos ZCL “Manufacturer Name” (0x0004) y “Model Identifier” (0x0005) del cluster Basic (0x0000).
- Si detecta fabricante “IKEA” y/o modelo “TRÅDFRI”, lo avisa por log (ALERTA).

Mantiene el código y la configuración simples y auto-contenidos.

## Requisitos

- ESP-IDF 5.5.1 (o compatible) con soporte para ESP32‑C6.
- Usar la terminal de ESP‑IDF en Windows (ESP-IDF PowerShell) para que `idf.py` esté en el PATH.
- Conexión serie al dispositivo (por ejemplo COM6).

## Archivos clave

- `main/main.c`: lógica de Coordinador + descubrimiento y lectura ZCL.
- `main/CMakeLists.txt`: declara el componente principal.
- `main/idf_component.yml`: usa componentes gestionados `espressif/esp-zigbee-lib` y `espressif/esp-zboss-lib`.
- `partitions.csv`: tabla de particiones con `zb_storage`/`zb_fct` para persistencia Zigbee.
- `sdkconfig.defaults`: objetivo `esp32c6`, Zigbee habilitado, rol ZC/ZR, radio nativa IEEE 802.15.4, y tabla de particiones personalizada.

## Compilar y flashear (Windows PowerShell de ESP‑IDF)

Abre la “ESP-IDF PowerShell” (no la PowerShell normal), navega a la carpeta del proyecto y ejecuta:

```powershell
idf.py set-target esp32c6
idf.py fullclean
idf.py reconfigure
idf.py build
idf.py -p COM6 flash monitor
```

Sustituye `COM6` por tu puerto serie. `reconfigure` aplica `sdkconfig.defaults` (rol de Coordinador, etc.).

## Emparejar la bombilla IKEA TRÅDFRI

1. Pon la bombilla en modo emparejamiento (consulta el método de reset del modelo TRÅDFRI; típicamente ciclo de encendido/apagado rápido varias veces hasta parpadeo).
2. Asegúrate de que el Coordinador esté en “steering” (la app abre la red automáticamente tras formar la red). Si la ventana de emparejamiento se cierra, reinicia la placa para reintentar.

## Qué esperar en el monitor

```text
I (xxx) ZB_SCAN: ZDO signal: ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP (...), status: ESP_OK
I (xxx) ZB_SCAN: Formando red (BDB network formation)...
I (xxx) ZB_SCAN: Red formada. Abriendo red para emparejamiento (steering 180s)...
I (xxx) ZB_SCAN: DEVICE_ANNCE: short=0xABCD ieee=... cap=0x.. 
I (xxx) ZB_SCAN: SimpleDesc: ep=1 profile=0x0104 device=0x0100
I (xxx) ZB_SCAN: Leyendo Basic attrs (tsn=..) a 0xABCD/ep1
I (xxx) ZB_SCAN: Basic attr 0x0004='IKEA of Sweden' (src 0xABCD ep1)
I (xxx) ZB_SCAN: Basic attr 0x0005='TRADFRI bulb E27 ...' (src 0xABCD ep1)
W (xxx) ZB_SCAN: ALERTA: Detectada bombilla IKEA TRÅDFRI (0xABCD ep1)
```

## Personalización

 Duración de la alerta: `ALERT_DURATION_MS` en `main/main.c` (por defecto 15000 ms)
- Volumen del zumbador: `BUZZER_VOLUME_PCT` (0–100) en `main/main.c` (usa PWM LEDC)
- idf.py no se encuentra / errores de build en PowerShell normal:
   - Usa la terminal “ESP-IDF PowerShell” para que el entorno esté configurado.

- El build falla con símbolos Green Power o enlaces de Zigbee:
   - Asegúrate de que el rol sea ZC/ZR (`CONFIG_ZB_ZCZR=y`) y Green Power esté deshabilitado (por defecto en este proyecto). Ejecuta `idf.py reconfigure`.

- La bombilla no se une:
   - Verifica que está en modo emparejamiento.
   - La ventana de “steering” dura ~180 s tras formar red. Reinicia la placa para abrir de nuevo la red.

- No ves la “ALERTA” pero sí DEVICE_ANNCE:
   - Algunos dispositivos usan otros endpoints; este ejemplo consulta el primer endpoint. Puedes ampliar la lógica para iterar todos los endpoints devueltos por Active EP.

## Licencia

Ejemplo de uso didáctico basado en ESP‑IDF y las librerías Zigbee de Espressif.
