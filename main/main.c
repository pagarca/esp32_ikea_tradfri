// Zigbee Coordinator example for ESP32-C6
// - Inicia la pila Zigbee en modo no-autostart
// - Forma una red (BDB network formation) y la abre a emparejamientos
// - Detecta dispositivos que se unan y, si son IKEA TRÅDFRI, levanta alerta

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_zigbee_core.h"
#include "platform/esp_zigbee_platform.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "nwk/esp_zigbee_nwk.h"
// Añadimos utilidades HA para crear un endpoint local mínimo
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "ZB_SCAN";

// Channel mask: channels 11..26
#define ZB_SCAN_CHANNEL_MASK  (0x07FFF800)
// Scan duration (beacon intervals): time per channel = ((1<<d)+1) * 15.36 ms
// Para scans ZDO puntuales (opcional)
#define ZB_SCAN_DURATION      (4)  // ~ (16+1)*15.36ms ≈ 261 ms por canal

static void zb_start_active_scan(uint8_t param);
static void try_read_basic_attrs(uint16_t nwk_addr, uint8_t endpoint);
static void active_ep_cb(esp_zb_zdp_status_t zdo_status, uint8_t ep_count, uint8_t *ep_id_list, void *user_ctx);
static void simple_desc_cb(esp_zb_zdp_status_t zdo_status, esp_zb_af_simple_desc_1_1_t *simple_desc, void *user_ctx);
static esp_err_t zcl_action_handler(esp_zb_core_action_callback_id_t cb_id, const void *message);
static void reopen_steering_cb(uint8_t param);

static bool contains_word_ci(const char *haystack, const char *needle)
{
	if (!haystack || !needle) return false;
	size_t nlen = strlen(needle);
	if (nlen == 0) return true;
	for (const char *p = haystack; *p; ++p) {
		size_t i = 0;
		while (i < nlen && p[i] && (char)tolower((unsigned char)p[i]) == (char)tolower((unsigned char)needle[i])) {
			i++;
		}
		if (i == nlen) return true;
	}
	return false;
}

// Evita alertar dos veces por el mismo dispositivo
static bool has_alerted_for(uint16_t short_addr)
{
	static uint16_t alerted[16];
	static uint8_t count = 0;
	for (uint8_t i = 0; i < count; i++) {
		if (alerted[i] == short_addr) return true;
	}
	return false;
}

static void mark_alerted_for(uint16_t short_addr)
{
	static uint16_t alerted[16];
	static uint8_t count = 0;
	// Buscar si ya está
	for (uint8_t i = 0; i < count; i++) {
		if (alerted[i] == short_addr) return;
	}
	if (count < (uint8_t)(sizeof(alerted)/sizeof(alerted[0]))) {
		alerted[count++] = short_addr;
	} else {
		// Política simple: sobreescribir circularmente
		static uint8_t idx = 0;
		alerted[idx++ % (uint8_t)(sizeof(alerted)/sizeof(alerted[0]))] = short_addr;
	}
}

// Scan complete callback: logs discovered networks
static void zb_scan_complete_cb(esp_zb_zdp_status_t zdo_status, uint8_t count,
								esp_zb_network_descriptor_t *nwk_list)
{
	ESP_LOGI(TAG, "Escaneo completado: status=%d, redes encontradas=%d", zdo_status, count);
	for (uint8_t i = 0; i < count; i++) {
		esp_zb_network_descriptor_t *d = &nwk_list[i];
		ESP_LOGI(TAG,
				 "[%u] CH=%u PAN_ID=0x%04X EPN=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X PJ=%d RC=%d EC=%d",
				 i,
				 d->logic_channel,
				 d->short_pan_id,
				 d->extended_pan_id[7], d->extended_pan_id[6], d->extended_pan_id[5], d->extended_pan_id[4],
				 d->extended_pan_id[3], d->extended_pan_id[2], d->extended_pan_id[1], d->extended_pan_id[0],
				 d->permit_joining, d->router_capacity, d->end_device_capacity);
	}
	if (count == 0) {
		ESP_LOGW(TAG, "No se han encontrado redes Zigbee.");
	}
	// Puede volver a programar otro escaneo si se desea (por ejemplo, cada 10s)
	esp_zb_scheduler_alarm(zb_start_active_scan, 0, 1000);
}

// App signal handler required by Zigbee SDK
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_s)
{
	uint32_t *sg = signal_s->p_app_signal;
	esp_zb_app_signal_type_t sig = *sg;
	esp_err_t st = signal_s->esp_err_status;

	ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
			 esp_zb_zdo_signal_to_string(sig), (unsigned)sig, esp_err_to_name(st));

	switch (sig) {
	case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
		// La pila está lista: formamos red como ZC y abrimos la red a emparejamientos
		ESP_LOGI(TAG, "Formando red (BDB network formation)...");
		esp_zb_set_bdb_commissioning_mode(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
		ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION));
		break;
	case ESP_ZB_BDB_SIGNAL_FORMATION:
		if (st == ESP_OK) {
			uint8_t ch = esp_zb_get_current_channel();
			ESP_LOGI(TAG, "Red formada en canal %u. Abriendo red para emparejamiento (steering 180s)...", ch);
			esp_zb_set_bdb_commissioning_mode(ESP_ZB_BDB_MODE_NETWORK_STEERING);
			ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING));
		} else {
			ESP_LOGE(TAG, "Fallo al formar red (status=%s). Reintentando en 3s", esp_err_to_name(st));
			esp_zb_scheduler_alarm(NULL, 0, 3000); // placeholder si quisiéramos reintentar
		}
		break;
	case ESP_ZB_BDB_SIGNAL_STEERING:
		if (st == ESP_OK) {
			ESP_LOGI(TAG, "Steering completado. Mantendremos la red abierta reintentando steering periódicamente.");
			// Reabrir steering cada 60s para facilitar el join si se pierde la ventana
			esp_zb_scheduler_alarm(reopen_steering_cb, 0, 60000);
		} else {
			ESP_LOGW(TAG, "Steering fallido o cancelado (%s). Reintentando en 10s", esp_err_to_name(st));
			esp_zb_scheduler_alarm(reopen_steering_cb, 0, 10000);
		}
		break;
	case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
		// Un dispositivo ha anunciado su presencia tras unirse/reunirse
		esp_zb_zdo_signal_device_annce_params_t *p = (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(sg);
		if (p) {
			ESP_LOGI(TAG, "DEVICE_ANNCE: short=0x%04X ieee=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X cap=0x%02X",
					p->device_short_addr,
					p->ieee_addr[7], p->ieee_addr[6], p->ieee_addr[5], p->ieee_addr[4],
					p->ieee_addr[3], p->ieee_addr[2], p->ieee_addr[1], p->ieee_addr[0],
					p->capability);
			esp_zb_zdo_active_ep_req_param_t aep = {.addr_of_interest = p->device_short_addr};
			ESP_LOGI(TAG, "Solicitando ActiveEP a 0x%04X", p->device_short_addr);
			esp_zb_zdo_active_ep_req(&aep, active_ep_cb, (void *)(uintptr_t)p->device_short_addr);
		} else {
			ESP_LOGW(TAG, "DEVICE_ANNCE sin parámetros. Ignorando");
		}
		}
		break;
	default:
		break;
	}
}

static void zb_start_active_scan(uint8_t param)
{
	(void)param;
	ESP_LOGI(TAG, "Iniciando escaneo activo Zigbee: mask=0x%08lX dur=%u",
			 (unsigned long)ZB_SCAN_CHANNEL_MASK, (unsigned)ZB_SCAN_DURATION);
	// Lanza escaneo activo; el callback se ejecutará desde la tarea Zigbee
	esp_zb_zdo_active_scan_request(ZB_SCAN_CHANNEL_MASK, ZB_SCAN_DURATION, zb_scan_complete_cb);
}

static void active_ep_cb(esp_zb_zdp_status_t zdo_status, uint8_t ep_count, uint8_t *ep_id_list, void *user_ctx)
{
	uint16_t nwk_addr = (uint16_t)(uintptr_t)user_ctx;
	if (zdo_status != ESP_ZB_ZDP_STATUS_SUCCESS || ep_count == 0 || !ep_id_list) {
		ESP_LOGW(TAG, "ActiveEP fallo/ vacío: status=%d", zdo_status);
		return;
	}
	ESP_LOGI(TAG, "Endpoints activos de 0x%04X (%u):", nwk_addr, ep_count);
	for (uint8_t i = 0; i < ep_count; i++) {
		ESP_LOGI(TAG, "  - ep %u", ep_id_list[i]);
	}
	// Consultamos simple descriptor de todos los endpoints para encontrar Basic 0x0000
	for (uint8_t i = 0; i < ep_count; i++) {
		esp_zb_zdo_simple_desc_req_param_t sreq = {
			.addr_of_interest = nwk_addr,
			.endpoint = ep_id_list[i],
		};
		esp_zb_zdo_simple_desc_req(&sreq, simple_desc_cb, (void *)(uintptr_t)nwk_addr);
	}
}

static void simple_desc_cb(esp_zb_zdp_status_t zdo_status, esp_zb_af_simple_desc_1_1_t *sd, void *user_ctx)
{
	uint16_t nwk_addr = (uint16_t)(uintptr_t)user_ctx;
	if (zdo_status != ESP_ZB_ZDP_STATUS_SUCCESS || !sd) {
		ESP_LOGW(TAG, "SimpleDesc fallo: status=%d", zdo_status);
		return;
	}
	ESP_LOGI(TAG, "SimpleDesc: ep=%u profile=0x%04X device=0x%04X", sd->endpoint, sd->app_profile_id, sd->app_device_id);
	// Solo intentamos lectura de Basic en perfiles HA (0x0104). Evitamos ep 242 (Green Power)
	if (sd->app_profile_id == 0x0104) {
		// Intentamos lectura de Basic 0x0000 (Manufacturer Name 0x0004, Model Id 0x0005)
		try_read_basic_attrs(nwk_addr, sd->endpoint);
	} else {
		ESP_LOGI(TAG, "Perfil no-HA (0x%04X) en ep %u: omitimos lectura Basic", sd->app_profile_id, sd->endpoint);
	}
}

static void try_read_basic_attrs(uint16_t nwk_addr, uint8_t endpoint)
{
	static uint16_t attrs[] = {0x0004, 0x0005};
	esp_zb_zcl_read_attr_cmd_t cmd = {
		.zcl_basic_cmd = {
			.dst_addr_u = {.addr_short = nwk_addr},
			.dst_endpoint = endpoint,
			.src_endpoint = 1,
		},
		.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
		.clusterID = 0x0000,
		.manuf_specific = 0,
		.direction = 0,
		.dis_default_resp = 1,
		.manuf_code = 0,
		.attr_number = (uint8_t)(sizeof(attrs)/sizeof(attrs[0])),
		.attr_field = attrs,
	};
	uint8_t tsn = esp_zb_zcl_read_attr_cmd_req(&cmd);
	ESP_LOGI(TAG, "Leyendo Basic attrs (tsn=%u) a 0x%04X/ep%u", tsn, nwk_addr, endpoint);
}

static esp_err_t zcl_action_handler(esp_zb_core_action_callback_id_t cb_id, const void *message)
{
	if (cb_id == ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID) {
		const esp_zb_zcl_cmd_read_attr_resp_message_t *m = (const esp_zb_zcl_cmd_read_attr_resp_message_t *)message;
		const uint16_t cluster = m->info.cluster;
		if (cluster == 0x0000) {
			// Recorremos variables de respuesta
			bool any_match = false;
			for (esp_zb_zcl_read_attr_resp_variable_t *v = m->variables; v; v = v->next) {
				if (v->status != ESP_ZB_ZCL_STATUS_SUCCESS) continue;
				uint16_t attr_id = v->attribute.id;
				// Los atributos string llevan el primer byte como longitud
				if (v->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING ||
					v->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_LONG_CHAR_STRING) {
					const uint8_t *raw = (const uint8_t *)v->attribute.data.value;
					if (!raw) continue;
					uint16_t len = raw[0];
					static char buf[64];
					if (len >= sizeof(buf)) len = sizeof(buf) - 1;
					memcpy(buf, raw + 1, len);
					buf[len] = '\0';
					ESP_LOGI(TAG, "Basic attr 0x%04X='%s' (src 0x%04X ep%u)", attr_id, buf, m->info.src_address.u.short_addr, m->info.src_endpoint);
					bool is_ikea = contains_word_ci(buf, "ikea");
					bool is_tradfri = contains_word_ci(buf, "tradfri");
					if ((attr_id == 0x0004 && is_ikea) || (attr_id == 0x0005 && is_tradfri)) {
						any_match = true;
					}
				}
			}
			if (any_match) {
				uint16_t src = m->info.src_address.u.short_addr;
				if (!has_alerted_for(src)) {
					ESP_LOGW(TAG, "ALERTA: Detectada bombilla IKEA TRÅDFRI (0x%04X ep%u)", src, m->info.src_endpoint);
					mark_alerted_for(src);
				}
			}
		}
	}
	return ESP_OK;
}

static void zigbee_task(void *pv)
{
	// Configurar Zigbee como Coordinador para formar red propia
	esp_zb_cfg_t cfg = {
		.esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
		.install_code_policy = false,
		.nwk_cfg = {
			.zczr_cfg = {
				.max_children = 16,
			}
		}
	};
	esp_zb_init(&cfg);

	// Registrar un endpoint local mínimo (HA Configuration Tool) en ep=1
	// Esto proporciona Basic/Identify y un endpoint de origen válido para comandos ZCL
	esp_zb_configuration_tool_cfg_t ha_cfg = ESP_ZB_DEFAULT_CONFIGURATION_TOOL_CONFIG();
	esp_zb_ep_list_t *ep_list = esp_zb_configuration_tool_ep_create(1 /*endpoint id*/, &ha_cfg);
	ESP_ERROR_CHECK(esp_zb_device_register(ep_list));
	// Registrar handler de acciones ZCL (respuestas de lectura, etc.)
	esp_zb_core_action_handler_register(zcl_action_handler);

	// Canales permitidos
	esp_zb_set_primary_network_channel_set(ZB_SCAN_CHANNEL_MASK);

	// Arrancar la pila sin autostart; manejamos BDB en el signal handler
	ESP_ERROR_CHECK(esp_zb_start(false));

	// Ejecutar bucle principal de Zigbee (bloqueante)
	esp_zb_stack_main_loop();
}

static void reopen_steering_cb(uint8_t param)
{
	(void)param;
	ESP_LOGI(TAG, "Reabriendo red para emparejamiento (steering)…");
	esp_zb_set_bdb_commissioning_mode(ESP_ZB_BDB_MODE_NETWORK_STEERING);
	esp_err_t err = esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "No se pudo reabrir steering: %s. Reintentando en 15s", esp_err_to_name(err));
		esp_zb_scheduler_alarm(reopen_steering_cb, 0, 15000);
	}
}

void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_init());

	// Configuración de plataforma (radio nativo + host por defecto)
	esp_zb_platform_config_t platform_cfg = {
		.radio_config = {
			.radio_mode = ZB_RADIO_MODE_NATIVE,   // Usar radio IEEE 802.15.4 nativa del ESP32-C6
			// .radio_uart_config queda a cero (no se usa en modo nativo)
		},
		.host_config = {
			.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, // Sin conexión host
			// .host_uart_config queda a cero
		},
	};
	ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

	// Crear tarea Zigbee (stack más holgado)
	xTaskCreate(zigbee_task, "zigbee_main", 7168, NULL, 5, NULL);
}
