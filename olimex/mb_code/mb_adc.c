#include "mb_main.h"

#if MB_ADC_ENABLE

#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "gpio.h"

#include "json/jsonparse.h"

#include "user_json.h"
#include "user_webserver.h"
#include "user_devices.h"
#include "user_timer.h"

#include "mb_app_config.h"
#include "mb_helper_library.h"

#ifdef MB_ADC_DEBUG
#undef MB_ADC_DEBUG
#define MB_ADC_DEBUG(...) debug(__VA_ARGS__);
#else
#define MB_ADC_DEBUG(...)
#endif

LOCAL uint16 adc_value_raw = 0;
LOCAL float adc_value = 0;
LOCAL char adc_value_str[15];

LOCAL mb_adc_config_t *p_adc_config;
LOCAL uint32 adc_refresh_timer = 0;
LOCAL uint8 mb_event_notified = false;

LOCAL void ICACHE_FLASH_ATTR mb_adc_read() {
	adc_value_raw = system_adc_read();
	
	adc_value = (adc_value_raw * p_adc_config->scale_k) + p_adc_config->scale_y;
	char tmp_str[20];
	uhl_flt2str(tmp_str, adc_value, p_adc_config->decimals);
	strncpy_null(adc_value_str, tmp_str, 15);

	MB_ADC_DEBUG("ADC read: raw:%d, val:%s\n", adc_value_raw, adc_value_str);
}

LOCAL void ICACHE_FLASH_ATTR mb_adc_set_response(char *response, bool is_fault, uint8 req_type) {
	char data_str[WEBSERVER_MAX_VALUE];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_ADC_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_ADC_DEBUG("ADC web response preparing\n");
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (req_type == MB_REQTYPE_POST) {
		char str_sc_k[20];
		char str_sc_y[20];
		char str_thr[20];
		char str_low[20];
		char str_hi[20];
		uhl_flt2str(str_sc_k, p_adc_config->scale_k, p_adc_config->decimals);
		uhl_flt2str(str_sc_y, p_adc_config->scale_y, p_adc_config->decimals);
		uhl_flt2str(str_thr, p_adc_config->threshold, p_adc_config->decimals);
		json_status(response, full_device_name, (adc_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Auto\": %d,"
					"\"Refresh\": %d,"
					"\"Each\": %d,"
					"\"Dec\": %d,"
					"\"Thr\": %s,"
					"\"ScK\": %s,"
					"\"ScY\": %s,"
					"\"Name\": \"%s\","
					"\"Post_type\": %d,"
					"\"Low\": %s,"
					"\"Hi\": %s"
				"}",
				p_adc_config->autostart,
				p_adc_config->refresh,
				p_adc_config->each,
				p_adc_config->decimals,
				str_thr,
				str_sc_k,
				str_sc_y,
				p_adc_config->name,
				p_adc_config->post_type,
				uhl_flt2str(str_low, p_adc_config->low, p_adc_config->decimals),
				uhl_flt2str(str_hi, p_adc_config->hi, p_adc_config->decimals)
			)
		);
		
	// event: do we want special format (thingspeak) (
	} else if (req_type==MB_REQTYPE_NONE && p_adc_config->post_type == MB_POSTTYPE_THINGSPEAK) {		// states change only
		json_sprintf(
			response,
			"%s{\"api_key\":\"%s\", \"%s\":%s}",
			MB_POSTTYPE_THINGSPEAK_STR,
			user_config_events_token(),
			(os_strlen(p_adc_config->name) == 0 ? "field1" : p_adc_config->name),
			adc_value_str);
			
	// event: special case - ifttt; measurement is evaluated before
	} else if (req_type==MB_REQTYPE_SPECIAL && p_adc_config->post_type == MB_POSTTYPE_IFTTT) {		// states change only
		char signal_name[30];
		signal_name[0] = 0x00;
		os_sprintf(signal_name, "%s", 
			(os_strlen(p_adc_config->name) == 0 ? "ADC" : p_adc_config->name));
		json_sprintf(
			response,
			"%s{\"value1\":\"%s\",\"value2\":\"%s\"}",
			MB_POSTTYPE_IFTTT_STR,
			signal_name,
			adc_value
		);

	// normal event measurement
	} else {
		json_data(
			response, full_device_name, (adc_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
				json_sprintf(data_str,
					"\"ADC\" : {\"ValueRaw\": %d, \"Value\" : %s}",
					adc_value_raw, adc_value_str),
				NULL
		);
	}
}

void ICACHE_FLASH_ATTR mb_adc_update() {
	LOCAL float adc_value_old = 0;
	LOCAL uint8 count = 0;
	char response[WEBSERVER_MAX_VALUE];
	
	mb_adc_read();
	count++;
	
	if (uhl_fabs(adc_value - adc_value_old) > p_adc_config->threshold || (count >= p_adc_config->each)) {
		MB_ADC_DEBUG("ADC: Change [%d] -> [%d]\n", adc_value_old, adc_value);
		adc_value_old = adc_value;
		count = 0;
		
		// Special handling; notify once only when limit exceeded
		if (p_adc_config->post_type == MB_POSTTYPE_IFTTT) {	// IFTTT limits check; make hysteresis to reset flag
			if (!mb_event_notified && ((uhl_fabs(p_adc_config->low - p_adc_config->hi) > 0.1f) && (adc_value < p_adc_config->low || adc_value > p_adc_config->hi))) {
				mb_event_notified = 1;
				mb_adc_set_response(response, false, MB_REQTYPE_SPECIAL);	
				user_event_raise(MB_DHT_URL, response);
			} else if (mb_event_notified && ((adc_value > p_adc_config->low + p_adc_config->threshold) && (adc_value < p_adc_config->hi -  p_adc_config->threshold))) {		// reset notification with hysteresis
				mb_event_notified = 0;
			}
		}
		
		mb_adc_set_response(response, false, MB_REQTYPE_NONE);
		
		user_event_raise(MB_ADC_URL, response);
	}
}

void ICACHE_FLASH_ATTR mb_adc_timer_init(bool start_cmd) {
	if (adc_refresh_timer != 0) {
		clearInterval(adc_refresh_timer);
	}
	
	if (p_adc_config->refresh == 0 || !start_cmd) {
		MB_ADC_DEBUG("ADC Timer not start!\n");
		adc_refresh_timer = 0;
	} else {
		adc_refresh_timer = setInterval(mb_adc_update, NULL, p_adc_config->refresh);
		MB_ADC_DEBUG("ADC setInterval: %d\n", p_adc_config->refresh);
	}
}

void ICACHE_FLASH_ATTR mb_adc_handler(
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
	
	mb_adc_config_t *p_config = p_adc_config;
	bool is_post = (method == POST);
	int start_cmd = -1;

	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					MB_ADC_DEBUG("ADC:JSON:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Refresh") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->refresh = jsonparse_get_value_as_int(&parser) * 1000;
					MB_ADC_DEBUG("ADC:JSON:Refresh:%d\n",p_config->refresh);
				} else if (jsonparse_strcmp_value(&parser, "Each") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					MB_ADC_DEBUG("ADC:JSON:Each:%d\n",p_config->each);
				} else if (jsonparse_strcmp_value(&parser, "Thr") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:Thr: %s\n", uhl_flt2str(tmp_str, p_adc_config->threshold, p_adc_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "ScK") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_k = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:ScK; %s\n", uhl_flt2str(tmp_str, p_adc_config->scale_k, p_adc_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "ScY") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_y = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:ScY:%s\n", uhl_flt2str(tmp_str, p_adc_config->scale_y, p_adc_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Name") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name, MB_VARNAMEMAX);
					MB_ADC_DEBUG("ADC:JSON:Name:%s\n", p_config->name);
				} else if (jsonparse_strcmp_value(&parser, "Low") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->low = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:Low:%s\n", uhl_flt2str(tmp_str, p_config->low, p_adc_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Hi") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->hi = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:Hi:%s\n", uhl_flt2str(tmp_str, p_config->hi, p_adc_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Post_type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->post_type = jsonparse_get_value_as_int(&parser);
					MB_ADC_DEBUG("ADC:JSON:Post_type:%d\n", p_config->post_type);
				} else if (jsonparse_strcmp_value(&parser, "Dec") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					MB_ADC_DEBUG("ADC:JSON:Dec:%d\n",p_config->decimals);
				}

				else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					start_cmd = (jsonparse_get_value_as_int(&parser) == 1 ? 1 : 0);
					mb_event_notified = false;
					MB_ADC_DEBUG("ADC:Start:%d\n", start_cmd);
				}
			}
		}
		if (is_post && start_cmd != -1)
			mb_adc_timer_init(start_cmd == 1);
	}

	mb_adc_set_response(response, false, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
	
}

void ICACHE_FLASH_ATTR mb_adc_init(bool isStartReading) {
	p_adc_config = (mb_adc_config_t *)p_user_app_config_data->adc;		// set proper structure in app settings

	webserver_register_handler_callback(MB_ADC_URL, mb_adc_handler);
	device_register(NATIVE, 0, MB_ADC_URL, NULL, NULL);
	
	if (!user_app_config_is_config_valid())
	{
		p_adc_config->autostart = MB_ADC_AUTOSTART_DEFAULT;
		p_adc_config->refresh	= MB_ADC_REFRESH_DEFAULT;
		p_adc_config->each	= MB_ADC_EACH_DEFAULT;
		p_adc_config->decimals	= 3;
		p_adc_config->threshold= MB_ADC_THRESHOLD_DEFAULT;
		p_adc_config->scale_k = MB_ADC_SCALE_K_DEFAULT;
		p_adc_config->scale_y = MB_ADC_SCALE_Y_DEFAULT;
		p_adc_config->name[0] = 0x00;
		p_adc_config->post_type = 0;
		p_adc_config->low = 0.0f;
		p_adc_config->hi = 0.0f;

		isStartReading = (p_adc_config->autostart == 1);
	}
	
	if (isStartReading) {
		mb_adc_timer_init(true);
	}
}
#endif
