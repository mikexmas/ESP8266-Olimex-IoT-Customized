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

LOCAL void ICACHE_FLASH_ATTR mb_adc_read() {
	adc_value_raw = system_adc_read();
	
	adc_value = (adc_value_raw * p_adc_config->scale_k) + p_adc_config->scale_y;
	char tmp_str[20];
	uhl_flt2str(tmp_str, adc_value, 5);
	strncpy_null(adc_value_str, tmp_str, 15);

	MB_ADC_DEBUG("ADC read: raw:%d, val:%s\n", adc_value_raw, adc_value_str);
}

LOCAL void ICACHE_FLASH_ATTR mb_adc_set_response(char *response, bool is_fault, bool is_post) {
	char data_str[WEBSERVER_MAX_VALUE];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_ADC_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_ADC_DEBUG("ADC web response: configured ? %d\n"
		"    Running: %s\n"
		"    ValueRaw %d\n"
		"    Value %s\n", 
		1,
		(adc_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP),
		adc_value_raw, adc_value_str);
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (is_post) {
		char str_sc_k[20];
		char str_sc_y[20];
		char str_thr[20];
		uhl_flt2str(str_sc_k, p_adc_config->scale_k, 6);
		uhl_flt2str(str_sc_y, p_adc_config->scale_y, 6);
		uhl_flt2str(str_thr, p_adc_config->threshold, 6);
		json_status(response, full_device_name, (adc_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Auto\": %d,"
					"\"Refresh\": %d,"
					"\"Each\": %d,"
					"\"Thr\": %s,"
					"\"ScK\": %s,"
					"\"ScY\": %s,"
					"\"Name\": \"%s\""
				"}",
				p_adc_config->autostart,
				p_adc_config->refresh,
				p_adc_config->each,
				str_thr,
				str_sc_k,
				str_sc_y,
				p_adc_config->name
			)
		);
		
	// event: do we want special format (thingspeak) (
	} else if (user_config_events_post_format() == USER_CONFIG_EVENTS_FORMAT_THINGSPEAK) {
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%s}",
			user_config_events_token(),
			(os_strlen(p_adc_config->name) == 0 ? "field1" : p_adc_config->name),
			adc_value_str);
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
	
	if (abs(adc_value - adc_value_old) > p_adc_config->threshold || (count >= p_adc_config->each)) {
		MB_ADC_DEBUG("ADC: Change [%d] -> [%d]\n", adc_value_old, adc_value);

		adc_value_old = adc_value;
		count = 0;
		
		mb_adc_set_response(response, false, false);
		
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
					MB_ADC_DEBUG("ADC:JSON:Thr: %s\n", uhl_flt2str(tmp_str, p_adc_config->threshold, 5));
				} else if (jsonparse_strcmp_value(&parser, "ScK") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_k = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:ScK; %s\n", uhl_flt2str(tmp_str, p_adc_config->scale_k, 5));
				} else if (jsonparse_strcmp_value(&parser, "ScY") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_y = uhl_jsonparse_get_value_as_float(&parser);
					MB_ADC_DEBUG("ADC:JSON:ScY:%s\n", uhl_flt2str(tmp_str, p_adc_config->scale_y, 5));
				} else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					int tmpisStart = jsonparse_get_value_as_int(&parser);
					mb_adc_timer_init(tmpisStart == 1);
					MB_ADC_DEBUG("ADC:Start:%s\n", (adc_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP));
				} else if (jsonparse_strcmp_value(&parser, "Name") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name, MB_VARNAMEMAX);
					MB_ADC_DEBUG("ADC:JSON:Name:%s\n", p_config->name);
				}
				
				// float testing
				else if (jsonparse_strcmp_value(&parser, "Test") == 0) {
					debug("ADC: TEST\n");
					char outStr[15];

					char *tmpStr = "-123.456";
					float tmpFloat = uhl_str2flt(tmpStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST1:%s\n",outStr);
					
					tmpStr = " -123.456";
					tmpFloat = uhl_str2flt(tmpStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST2:%s\n",outStr);

					tmpStr = " 123.456 ";
					tmpFloat = uhl_str2flt(tmpStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST3:%s\n",outStr);

					tmpStr = " -123.456 ";
					tmpFloat = uhl_str2flt(tmpStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST4:%s\n",outStr);

				} else if (jsonparse_strcmp_value(&parser, "Float") == 0) {
					debug("\nFLT TEST\n");
					char outStr[15];

					char *tmpStr = "";
					float tmpFloat = 123.456;
					uhl_flt2str(outStr, tmpFloat, 2);
					debug("\nADC TEST1a:%s\n",outStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST1b:%s\n",outStr);
					uhl_flt2str(outStr, tmpFloat, 4);
					debug("\nADC TEST1c:%s\n",outStr);
					
					tmpFloat = -123.456;
					uhl_flt2str(outStr, tmpFloat, 2);
					debug("\nADC TEST2a:%s\n",outStr);
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST2b:%s\n",outStr);
					uhl_flt2str(outStr, tmpFloat, 4);
					debug("\nADC TEST2c:%s\n",outStr);

					tmpFloat = 0.567;
					uhl_flt2str(outStr, tmpFloat, 3);
					debug("\nADC TEST3a:%s\n",outStr);
					
					tmpFloat = 0.00567;
					uhl_flt2str(outStr, tmpFloat, 6);
					debug("\nADC TEST4:%s\n",outStr);
				} 
			}
		}
	}

	mb_adc_set_response(response, false, true);
	
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
		p_adc_config->threshold= MB_ADC_THRESHOLD_DEFAULT;
		p_adc_config->scale_k = MB_ADC_SCALE_K_DEFAULT;
		p_adc_config->scale_y = MB_ADC_SCALE_Y_DEFAULT;
		p_adc_config->name[0] = 0x00;

		isStartReading = (p_adc_config->autostart == 1);
	}
	
	if (isStartReading) {
		mb_adc_timer_init(true);
	}
}
#endif
