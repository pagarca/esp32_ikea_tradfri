// Zigbee Coordinator example for ESP32-C6
// - Start the Zigbee stack in no-autostart mode
// - Form a network (BDB network formation) and open it for joining
// - Detect devices that join and raise an alert if they are IKEA TRÅDFRI

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_zigbee_core.h"
#include "platform/esp_zigbee_platform.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "nwk/esp_zigbee_nwk.h"
// HA utilities to create a minimal local endpoint
#include "ha/esp_zigbee_ha_standard.h"
// On-board RGB LED (WS2812) driven via RMT
#include "led_strip.h"
#include "freertos/timers.h"

static const char *TAG = "ZB_SCAN";

// Channel mask: channels 11..26
#define ZB_SCAN_CHANNEL_MASK  (0x07FFF800)
// Scan duration (beacon intervals): time per channel = ((1<<d)+1) * 15.36 ms
// For occasional ZDO scans (optional)
#define ZB_SCAN_DURATION      (4)  // ~ (16+1)*15.36ms ≈ 261 ms per channel

// Assumption: the board has a WS2812 RGB LED on GPIO 8 (ESP32-C6 DevKitC)
#ifndef BOARD_RGB_LED_GPIO
#define BOARD_RGB_LED_GPIO     (8)
#endif
// Active buzzer on GPIO 10 (logic high = ON)
#ifndef BUZZER_GPIO
#define BUZZER_GPIO            (10)
#endif

// Alert duration (LED red + active buzzer) in milliseconds
#ifndef ALERT_DURATION_MS
#define ALERT_DURATION_MS       (10 * 1000) // 10 segundos
#endif

// Active buzzer volume percentage (0..100). Implemented with LEDC PWM
#ifndef BUZZER_VOLUME_PCT
#define BUZZER_VOLUME_PCT       (75)
#endif

// PWM configuration for the buzzer
#ifndef BUZZER_PWM_FREQ_HZ
#define BUZZER_PWM_FREQ_HZ      (5000) // 5 kHz
#endif
#ifndef BUZZER_LEDC_MODE
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#endif
#ifndef BUZZER_LEDC_TIMER
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#endif
#ifndef BUZZER_LEDC_CHANNEL
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#endif
#ifndef BUZZER_LEDC_DUTY_RES
#define BUZZER_LEDC_DUTY_RES    LEDC_TIMER_10_BIT // 10 bits -> 1023 max
#endif

static led_strip_handle_t s_led_strip = NULL;
static TimerHandle_t s_led_timer = NULL; // timer to return to green after the configured duration
static TimerHandle_t s_buzzer_timer = NULL; // buzzer blink timer during alert
static volatile bool s_buzzer_state = false;
static uint32_t s_buzzer_on_duty = 0;       // duty for the configured volume

// Track devices already alerted to avoid duplicates
static uint16_t s_alerted[16];
static uint8_t s_alerted_count = 0;
static uint8_t s_alerted_wr_idx = 0;

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

static void led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	if (!s_led_strip) return;
	// Set color of the first pixel and refresh
	// Some WS2812 boards use GRB order; swap R<->G to correct colors
	(void)led_strip_set_pixel(s_led_strip, 0, g, r, b);
	(void)led_strip_refresh(s_led_strip);
}

static void led_timer_cb(TimerHandle_t xTimer)
{
	(void)xTimer;
	// Return to green (idle)
	led_set_rgb(0, 255, 0);
	// Active buzzer: turn off PWM (duty 0)
	(void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
	(void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
	// Stop buzzer blink
	if (s_buzzer_timer) {
		xTimerStop(s_buzzer_timer, 0);
	}
}

static void led_init(void)
{
	// Configure LED strip device with RMT
	led_strip_config_t strip_config = {
		.strip_gpio_num = BOARD_RGB_LED_GPIO,
		.max_leds = 1,
		.led_model = LED_MODEL_WS2812,
		.flags.invert_out = false,
	};
	led_strip_rmt_config_t rmt_config = {
		.resolution_hz = 10 * 1000 * 1000, // 10MHz
		.flags.with_dma = false,
	};
	esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "No se pudo inicializar LED RGB (gpio=%d): %s", BOARD_RGB_LED_GPIO, esp_err_to_name(err));
		s_led_strip = NULL;
		return;
	}
	(void)led_strip_clear(s_led_strip);
	// Create one-shot timer with the configured alert duration
	s_led_timer = xTimerCreate("led_to_green", pdMS_TO_TICKS(ALERT_DURATION_MS), pdFALSE, NULL, led_timer_cb);
	if (s_led_timer == NULL) {
		ESP_LOGW(TAG, "No se pudo crear temporizador de LED");
	}
	// Idle state: green
	led_set_rgb(0, 255, 0);
}

static void buzzer_init(void)
{
	// Configure LEDC for PWM on the buzzer GPIO
	ledc_timer_config_t tcfg = {
		.speed_mode = BUZZER_LEDC_MODE,
		.duty_resolution = BUZZER_LEDC_DUTY_RES,
		.timer_num = BUZZER_LEDC_TIMER,
		.freq_hz = BUZZER_PWM_FREQ_HZ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	esp_err_t err = ledc_timer_config(&tcfg);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "LEDC timer cfg fallo: %s", esp_err_to_name(err));
	}
	ledc_channel_config_t ccfg = {
		.gpio_num = BUZZER_GPIO,
		.speed_mode = BUZZER_LEDC_MODE,
		.channel = BUZZER_LEDC_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = BUZZER_LEDC_TIMER,
		.duty = 0,
		.hpoint = 0,
		.flags = { .output_invert = 0 },
	};
	err = ledc_channel_config(&ccfg);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "LEDC channel cfg fallo: %s", esp_err_to_name(err));
	}
	// Compute duty for the configured volume
	uint32_t max_duty = (1U << (int)BUZZER_LEDC_DUTY_RES) - 1U;
	uint32_t pct = (BUZZER_VOLUME_PCT > 100) ? 100 : BUZZER_VOLUME_PCT;
	s_buzzer_on_duty = (max_duty * pct) / 100U;
	// Ensure initial OFF
	(void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
	(void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void buzzer_toggle_cb(TimerHandle_t xTimer)
{
	(void)xTimer;
	s_buzzer_state = !s_buzzer_state;
	if (s_buzzer_state) {
		(void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, s_buzzer_on_duty);
		(void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
	} else {
		(void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
		(void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
	}
}

static void buzzer_timer_create(void)
{
	// Periodic timer for 2 Hz blink (toggle every 250 ms)
	if (!s_buzzer_timer) {
		s_buzzer_timer = xTimerCreate("buzz_tgl", pdMS_TO_TICKS(250), pdTRUE, NULL, buzzer_toggle_cb);
	}
}

// Avoid alerting twice for the same device
static bool has_alerted_for(uint16_t short_addr)
{
	for (uint8_t i = 0; i < s_alerted_count; i++) {
		if (s_alerted[i] == short_addr) return true;
	}
	return false;
}

static void mark_alerted_for(uint16_t short_addr)
{
	for (uint8_t i = 0; i < s_alerted_count; i++) {
		if (s_alerted[i] == short_addr) return;
	}
	if (s_alerted_count < (uint8_t)(sizeof(s_alerted)/sizeof(s_alerted[0]))) {
		s_alerted[s_alerted_count++] = short_addr;
	} else {
		// Simple policy: circular overwrite
		s_alerted[s_alerted_wr_idx++ % (uint8_t)(sizeof(s_alerted)/sizeof(s_alerted[0]))] = short_addr;
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
	// Optionally schedule another scan (e.g., every 1s)
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
		// Stack is ready: form a network as Coordinator and open for joining
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
			esp_zb_scheduler_alarm(NULL, 0, 3000); // placeholder to retry later if desired
		}
		break;
	case ESP_ZB_BDB_SIGNAL_STEERING:
		if (st == ESP_OK) {
			ESP_LOGI(TAG, "Steering completado. Mantendremos la red abierta reintentando steering periódicamente.");
			// Re-open steering every 60s to make joining easier if the window was missed
			esp_zb_scheduler_alarm(reopen_steering_cb, 0, 60000);
		} else {
			ESP_LOGW(TAG, "Steering fallido o cancelado (%s). Reintentando en 10s", esp_err_to_name(st));
			esp_zb_scheduler_alarm(reopen_steering_cb, 0, 10000);
		}
		break;
	case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
		// A device announced its presence after joining/rejoining
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
	// Launch active scan; callback will run from the Zigbee task
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
	// Request Simple Descriptor for all endpoints to find Basic 0x0000
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
	// Only try to read Basic on HA profile endpoints (0x0104). Skip ep 242 (Green Power)
	if (sd->app_profile_id == 0x0104) {
		// Try reading Basic 0x0000 (Manufacturer Name 0x0004, Model Id 0x0005)
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
			// Iterate response variables
			bool any_match = false;
			for (esp_zb_zcl_read_attr_resp_variable_t *v = m->variables; v; v = v->next) {
				if (v->status != ESP_ZB_ZCL_STATUS_SUCCESS) continue;
				uint16_t attr_id = v->attribute.id;
				// String attributes carry the first byte as length
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
				// Set LED red for the configured duration whenever detected
				led_set_rgb(255, 0, 0);
				if (s_led_timer) {
					xTimerStop(s_led_timer, 0);
					xTimerStart(s_led_timer, 0);
				}
				// Active buzzer: start 2 Hz blinking
				buzzer_timer_create();
				// Ensure starting in ON state (duty according to volume) and state coherence
				(void)ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, s_buzzer_on_duty);
				(void)ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
				s_buzzer_state = true; // siguiente toggle -> OFF
				if (s_buzzer_timer) {
					xTimerStop(s_buzzer_timer, 0);
					xTimerStart(s_buzzer_timer, 0);
				}
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
	// Configure Zigbee as Coordinator to form our own network
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

	// Register a minimal local endpoint (HA Configuration Tool) on ep=1
	// Provides Basic/Identify and a valid source endpoint for ZCL commands
	esp_zb_configuration_tool_cfg_t ha_cfg = ESP_ZB_DEFAULT_CONFIGURATION_TOOL_CONFIG();
	esp_zb_ep_list_t *ep_list = esp_zb_configuration_tool_ep_create(1 /*endpoint id*/, &ha_cfg);
	ESP_ERROR_CHECK(esp_zb_device_register(ep_list));
	// Register ZCL core action handler (read responses, etc.)
	esp_zb_core_action_handler_register(zcl_action_handler);

	// Allowed channels
	esp_zb_set_primary_network_channel_set(ZB_SCAN_CHANNEL_MASK);

	// Start the stack without autostart; handle BDB in the signal handler
	ESP_ERROR_CHECK(esp_zb_start(false));

	// Run Zigbee main loop (blocking)
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

	// Platform configuration (native radio + default host)
	esp_zb_platform_config_t platform_cfg = {
		.radio_config = {
			.radio_mode = ZB_RADIO_MODE_NATIVE,   // Use ESP32-C6 native IEEE 802.15.4 radio
			// .radio_uart_config left zero (unused in native mode)
		},
		.host_config = {
			.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, // No host connection
			// .host_uart_config left zero
		},
	};
	ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

	// Initialize on-board RGB LED (if present)
	led_init();
	// Initialize active buzzer (PWM on GPIO)
	buzzer_init();

	// Create Zigbee task (larger stack)
	xTaskCreate(zigbee_task, "zigbee_main", 7168, NULL, 5, NULL);
}
