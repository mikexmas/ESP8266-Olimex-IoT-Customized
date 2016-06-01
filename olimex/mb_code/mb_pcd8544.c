#include "mb_main.h"

#if MB_PCD8544_ENABLE

#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "queue.h"
#include "gpio.h"

#include "json/jsonparse.h"

#include "user_json.h"
#include "user_misc.h"
#include "user_webserver.h"
#include "user_devices.h"

#include "mb_app_config.h"
#include "mb_helper_library.h"
#include "pcd8544/pcd8544.h"	// we use EDAF driver
#include "mb_pcd8544.h"

#ifdef MB_PCD8544_DEBUG
#undef MB_PCD8544_DEBUG
#define MB_PCD8544_DEBUG(...) debug(__VA_ARGS__);
#else
#define MB_PCD8544_DEBUG(...)
#endif

LOCAL mb_pcd8544_config_t* mb_p_pcd8544_config;

//#if MB_ACTIONS_ENABLE
//mb_action_data_t mb_ping_action_data;
//#endif

void ICACHE_FLASH_ATTR mb_pcd8544_hw_init() {
	PCD8544_Settings pcd8544_settings;
	pcd8544_settings.resetPin = mb_p_pcd8544_config->resetPin;
	pcd8544_settings.scePin = mb_p_pcd8544_config->scePin;
	pcd8544_settings.dcPin = mb_p_pcd8544_config->dcPin;
	pcd8544_settings.sdinPin = mb_p_pcd8544_config->sdinPin;
	pcd8544_settings.sclkPin = mb_p_pcd8544_config->sclkPin;
	PCD8544_init(&pcd8544_settings);
}

/* Make response */
LOCAL void ICACHE_FLASH_ATTR mb_pcd8544_set_response(char *response, bool is_fault, uint8 req_type) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_PCD8544_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_PCD8544_DEBUG("PCD8544:Resp.prep:%d;isFault:%d:\n", req_type, is_fault)
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (req_type == MB_REQTYPE_POST) {
		json_status(response, full_device_name, DEVICE_STATUS_OK, 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Reset_pin\": %d,"
					"\"Sce_pin\":%d,"
					"\"Dc_pin\": %d,"
					"\"Sdin_pin\": %d,"
					"\"Sclk_pin\": %d"
				"}",
				mb_p_pcd8544_config->resetPin,
				mb_p_pcd8544_config->scePin,
				mb_p_pcd8544_config->dcPin,
				mb_p_pcd8544_config->sdinPin,
				mb_p_pcd8544_config->sclkPin
			)
		);

	// normal event measurement
	} else {
		json_data(
			response, full_device_name, DEVICE_STATUS_OK, 
				json_sprintf(data_str,
					"\"pcd8544\": {"
					"}"
				),
				NULL
		);
	}
}

void ICACHE_FLASH_ATTR mb_pcd8544_handler(
	struct espconn *pConnection, 
	request_method method, 
	char *url, 
	char *data, 
	uint16 data_len, 
	uint32 content_len, 
	char *response,
	uint16 response_len
) {
	struct jsonparse_state parser;
	int type;
	char tmp_str[20];
	
	mb_pcd8544_config_t *p_config = mb_p_pcd8544_config;
	bool is_post = (method == POST);
	int start_cmd = -1;	// 0=STOP, 1=START other none
	uint8 tmp_ret = 0xFF;
	
	// post config for INIT
	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Reset_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->resetPin = jsonparse_get_value_as_sint(&parser);
					MB_PCD8544_DEBUG("PCD8544:CFG:Reset_pin:%d\n", p_config->resetPin);
				} else if (jsonparse_strcmp_value(&parser, "Sce_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scePin = jsonparse_get_value_as_sint(&parser);
					MB_PCD8544_DEBUG("PCD8544:CFG:Sce_pin:%d\n", p_config->scePin);
				} else if (jsonparse_strcmp_value(&parser, "Dc_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->dcPin = jsonparse_get_value_as_sint(&parser);
					MB_PCD8544_DEBUG("PCD8544:CFG:Dc_pin:%d\n", p_config->dcPin);
				} else if (jsonparse_strcmp_value(&parser, "Sdin_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->sdinPin = jsonparse_get_value_as_sint(&parser);
					MB_PCD8544_DEBUG("PCD8544:CFG:Sdin_pin:%d\n", p_config->sdinPin);
				} else if (jsonparse_strcmp_value(&parser, "Sclk_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->sclkPin = jsonparse_get_value_as_sint(&parser);
					MB_PCD8544_DEBUG("PCD8544:CFG:Sclk_pin: %d\n", p_config->sclkPin);
				} else if (tmp_ret = user_app_config_handler_part(&parser) != 0xFF){	// check for common app commands
					MB_PCD8544_DEBUG("PCD8544:CFG:APPCONFIG:%d\n", tmp_ret);
				}
				else if (jsonparse_strcmp_value(&parser, "Goto_xy") == 0) {		// 11,22
					is_post = false;
					jsonparse_next(&parser);jsonparse_next(&parser);
					char tmp_goto[32];
					jsonparse_copy_value(&parser, tmp_goto, 32);
					char *p_comma = (char *)os_strstr(tmp_goto, ",");
					if (tmp_goto > 0 && p_comma - tmp_goto < 32) {
						char tmp_goto_[32];
						strncpy(tmp_goto_, tmp_goto, p_comma - tmp_goto - 1);
						int goto_x = atoi(tmp_goto_);
						tmp_goto_[0] = 0x00;
						strncpy(tmp_goto_, p_comma+1, 32);
						int goto_y = atoi(tmp_goto_);
						PCD8544_gotoXY(goto_x, goto_y);
						MB_PCD8544_DEBUG("PCD8544:Goto_x/y:%d/%d\n", goto_x, goto_y);
					}
					MB_PCD8544_DEBUG("PCD8544:Goto_x/y:NA\n");
				} else if (jsonparse_strcmp_value(&parser, "") == 0) {
					is_post = false;
					jsonparse_next(&parser);jsonparse_next(&parser);
					char tmp_str[128];
					jsonparse_copy_value(&parser, tmp_str, MB_VARNAMEMAX);
					PCD8544_lcdPrint(tmp_str);
					MB_PCD8544_DEBUG("PCD8544:lcdPrint:%s\n", tmp_str);
				}
			}
		}

		if (is_post) {
			mb_pcd8544_hw_init();
		}
	}
	
	mb_PCD8544_set_response(response, false, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
}

/* Main Initialization file
 * true: init HW & timer, else web service only (listener)
 */
void ICACHE_FLASH_ATTR mb_pcd8544_init() {
	bool isStartReading = false;
	mb_p_pcd8544_config = (mb_pcd8544_config_t *)p_user_app_config_data->pcd8544;		// set proper structure in app settings
	
	webserver_register_handler_callback(MB_PCD8544_URL, mb_pcd8544_handler);
	device_register(NATIVE, 0, MB_PCD8544_DEVICE, MB_PCD8544_URL, NULL, NULL);
	
	if (!user_app_config_is_config_valid())
	{
		mb_p_pcd8544_config->resetPin = MB_PCD8544_RESET_PIN_DEFAULT;
		mb_p_pcd8544_config->scePin = MB_PCD8544_SCE_PIN_DEFAULT;
		mb_p_pcd8544_config->dcPin = MB_PCD8544_DC_PIN_DEFAULT;
		mb_p_pcd8544_config->sdinPin = MB_PCD8544_SDIN_PIN_DEFAULT;
		mb_p_pcd8544_config->sclkPin = MB_PCD8544_SCLK_PIN_DEFAULT;
	}

	mb_pcd8544_hw_init();
}

#endif