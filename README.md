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

- Canales Zigbee: `ZB_SCAN_CHANNEL_MASK` (por defecto 11–26).
- Memoria tarea principal: `CONFIG_MAIN_TASK_STACK_SIZE` en `sdkconfig.defaults`.

## Solución de problemas

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
