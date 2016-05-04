#include "mb_main.h"
#include "mb_main.h"

#if MB_AIN_ENABLE

#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "gpio.h"

#include "json/jsonparse.h"

#include "user_json.h"
#include "user_webserver.h"
#include "user_devices.h"
#include "user_timer.h"

#include "mb_main.h"
#include "mb_app_config.h"
#include "mb_helper_library.h"

#ifdef MB_AIN_DEBUG
#undef MB_AIN_DEBUG
#define MB_AIN_DEBUG(...) debug(__VA_ARGS__);
#else
#define MB_AIN_DEBUG(...)
#endif

LOCAL uint16 ain_value_raw = 0;
LOCAL float ain_value = 0;
LOCAL char ain_value_str[15];

LOCAL mb_ain_config_t *p_ain_config;
LOCAL uint32 ain_refresh_timer = 0;
LOCAL char *ain_limits_notified_str = (char*)MB_LIMITS_NONE;
LOCAL uint8 ain_limits_notified = MB_LIMITS_NOTIFY_INIT;

#if MB_ACTIONS_ENABLE
mb_action_data_t mb_ain_action_data;
#endif

LOCAL void ICACHE_FLASH_ATTR mb_ain_read() {
	ain_value_raw = system_adc_read();
	
	ain_value = (ain_value_raw * p_ain_config->scale_k) + p_ain_config->scale_y;
	char tmp_str[20];
	uhl_flt2str(tmp_str, ain_value, p_ain_config->decimals);
	strncpy_null(ain_value_str, tmp_str, 15);

	MB_AIN_DEBUG("ADC read: raw:%d, val:%s\n", ain_value_raw, ain_value_str);
}

LOCAL void ICACHE_FLASH_ATTR mb_ain_set_response(char *response, bool is_fault, uint8 req_type) {
	char data_str[WEBSERVER_MAX_VALUE];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_AIN_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_AIN_DEBUG("ADC web response preparing:reqType:%d\n", req_type);
	
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
		uhl_flt2str(str_sc_k, p_ain_config->scale_k, p_ain_config->decimals);
		uhl_flt2str(str_sc_y, p_ain_config->scale_y, p_ain_config->decimals);
		uhl_flt2str(str_thr, p_ain_config->threshold, p_ain_config->decimals);
		json_status(response, full_device_name, (ain_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
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
				p_ain_config->autostart,
				p_ain_config->refresh,
				p_ain_config->each,
				p_ain_config->decimals,
				str_thr,
				str_sc_k,
				str_sc_y,
				p_ain_config->name,
				p_ain_config->post_type,
				uhl_flt2str(str_low, p_ain_config->low, p_ain_config->decimals),
				uhl_flt2str(str_hi, p_ain_config->hi, p_ain_config->decimals)
			)
		);
		
	// event: do we want special format (thingspeak) (
	} else if (req_type==MB_REQTYPE_NONE && p_ain_config->post_type == MB_POSTTYPE_THINGSPEAK) {		// states change only
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%s}",
			user_config_events_token(),
			(os_strlen(p_ain_config->name) == 0 ? "field1" : p_ain_config->name),
			ain_value_str);
			
	// event: special case - ifttt; measurement is evaluated before
	} else if (req_type==MB_REQTYPE_SPECIAL && p_ain_config->post_type == MB_POSTTYPE_IFTTT) {		// states change only
		char signal_name[30];
		signal_name[0] = 0x00;
		os_sprintf(signal_name, "%s", 
			(os_strlen(p_ain_config->name) == 0 ? "AIN" : p_ain_config->name));
		json_sprintf(
			response,
			"{\"value1\":\"%s\",\"value2\":\"%s\"}",
			signal_name,
			ain_value_str
		);

	// normal event measurement
	} else {
		json_data(
			response, full_device_name, (ain_refresh_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
				json_sprintf(data_str,
					"\"ADC\" : {\"ValueRaw\": %d, \"Value\" : %s}",
					ain_value_raw, ain_value_str),
				NULL
		);
	}
}

LOCAL void ICACHE_FLASH_ATTR mb_ain_update() {
	LOCAL float ain_value_old = 0;
	LOCAL uint8 count = 0;
	char response[WEBSERVER_MAX_VALUE];
	char ain_value_old_str[15];
	
	mb_ain_read();
	count++;
	
	// Allways check for limits crossing
	// Special handling; notify once only when limit exceeded
	if (p_ain_config->post_type == MB_POSTTYPE_IFTTT
#if MB_ACTIONS_ENABLE
		|| p_ain_config->action >= MB_ACTIONTYPE_FIRST && p_ain_config->action <= MB_ACTIONTYPE_LAST
#endif
		) {	// IFTTT limits check; make hysteresis to reset flag
		// Evaluate limits in some cases; we need later for eg IFTTT / internal action
		uint8 eval_val = 0x00;
		uint8 tmp_limits_notified;
		bool make_event = false;
		
		eval_val = uhl_which_event(ain_value, p_ain_config->hi, p_ain_config->low, p_ain_config->threshold, &ain_limits_notified_str);
		tmp_limits_notified = eval_val;
		make_event = false;
			
		if (ain_limits_notified != tmp_limits_notified && tmp_limits_notified != MB_LIMITS_NOTIFY_INIT)		// T
		{
			make_event = true;
			ain_limits_notified = tmp_limits_notified;
		}
				
		MB_AIN_DEBUG("AIN:Eval:%d,Notif:%d,Notif_str:%s,Make:%d\n",eval_val, ain_limits_notified, ain_limits_notified_str, make_event);

		if (make_event && p_ain_config->post_type == MB_POSTTYPE_IFTTT) {	// IFTTT limits check; make hysteresis to reset flag
			mb_ain_set_response(response, false, MB_REQTYPE_SPECIAL);
			webclient_post(user_config_events_ssl(), user_config_events_user(), user_config_events_password(), user_config_events_server(), user_config_events_ssl() ? WEBSERVER_SSL_PORT : WEBSERVER_PORT, user_config_events_path(), response);
		}
#if MB_ACTIONS_ENABLE			
		if (make_event && p_ain_config->action >= MB_ACTIONTYPE_FIRST && p_ain_config->action <= MB_ACTIONTYPE_LAST) {	// ACTION: DIO
			mb_ain_action_data.action_type = p_ain_config->action;
			if (ain_limits_notified == MB_LIMITS_NOTIFY_HI || ain_limits_notified == MB_LIMITS_NOTIFY_LOW)
				mb_ain_action_data.value = 1;
			else 
				mb_ain_action_data.value = 0;
			setTimeout((void *)mb_action_post, &mb_ain_action_data, 10);
		}
#endif
	}
	
	// calculate epsilon to determine min change => it depends on number of decimals
	//uint32 eps_uint = pow_int(10, p_ain_config->decimals);
	char eps_str[15];
	float eps = (1/(float)pow_int(10, p_ain_config->decimals));
	if ((uhl_fabs(ain_value - ain_value_old) > p_ain_config->threshold)
			|| (count >= p_ain_config->each && (uhl_fabs(ain_value - ain_value_old) > eps))
			|| (count >= 0xFF)
		) {
		MB_AIN_DEBUG("AIN:Change [%s]->[%s],eps:%s\n", uhl_flt2str(ain_value_old_str, ain_value_old, p_ain_config->decimals), ain_value_str, uhl_flt2str(eps_str, eps, p_ain_config->decimals));
		ain_value_old = ain_value;
		count = 0;
		
		// Thingspeak
		if (p_ain_config->post_type == MB_POSTTYPE_THINGSPEAK) {
			mb_ain_set_response(response, false, MB_REQTYPE_SPECIAL);	
			webclient_post(user_config_events_ssl(), user_config_events_user(), user_config_events_password(), user_config_events_server(), user_config_events_ssl() ? WEBSERVER_SSL_PORT : WEBSERVER_PORT, user_config_events_path(), response);
		}
		
		mb_ain_set_response(response, false, MB_REQTYPE_NONE);
		user_event_raise(MB_AIN_URL, response);
	}
}

LOCAL void ICACHE_FLASH_ATTR mb_ain_timer_init(bool start_cmd) {
	if (ain_refresh_timer != 0) {
		clearInterval(ain_refresh_timer);
	}
	
	if (p_ain_config->refresh == 0 || !start_cmd) {
		MB_AIN_DEBUG("ADC Timer not start!\n");
		ain_refresh_timer = 0;
	} else {
		ain_refresh_timer = setInterval(mb_ain_update, NULL, p_ain_config->refresh);
		MB_AIN_DEBUG("AIN:Timer:Start:%d\n", p_ain_config->refresh);
		ain_limits_notified = MB_LIMITS_NOTIFY_INIT;		// IFTTT notification flag reset
		ain_limits_notified_str = (char*)MB_LIMITS_NONE;
		mb_ain_update();									// instant measurement, otherwised we have to wait timer elapsed
	}
}

void ICACHE_FLASH_ATTR mb_ain_handler(
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
	
	mb_ain_config_t *p_config = p_ain_config;
	bool is_post = (method == POST);
	int start_cmd = -1;

	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					MB_AIN_DEBUG("AIN:CFG:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Refresh") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->refresh = jsonparse_get_value_as_int(&parser) * 1000;
					MB_AIN_DEBUG("AIN:CFG:Refresh:%d\n",p_config->refresh);
				} else if (jsonparse_strcmp_value(&parser, "Each") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					MB_AIN_DEBUG("AIN:CFG:Each:%d\n",p_config->each);
				} else if (jsonparse_strcmp_value(&parser, "Thr") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold = uhl_jsonparse_get_value_as_float(&parser);
					MB_AIN_DEBUG("AIN:CFG:Thr: %s\n", uhl_flt2str(tmp_str, p_ain_config->threshold, p_ain_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "ScK") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_k = uhl_jsonparse_get_value_as_float(&parser);
					MB_AIN_DEBUG("AIN:CFG:ScK; %s\n", uhl_flt2str(tmp_str, p_ain_config->scale_k, p_ain_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "ScY") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->scale_y = uhl_jsonparse_get_value_as_float(&parser);
					MB_AIN_DEBUG("AIN:CFG:ScY:%s\n", uhl_flt2str(tmp_str, p_ain_config->scale_y, p_ain_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Name") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name, MB_VARNAMEMAX);
					MB_AIN_DEBUG("AIN:CFG:Name:%s\n", p_config->name);
				} else if (jsonparse_strcmp_value(&parser, "Low") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->low = uhl_jsonparse_get_value_as_float(&parser);
					MB_AIN_DEBUG("AIN:CFG:Low:%s\n", uhl_flt2str(tmp_str, p_config->low, p_ain_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Hi") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->hi = uhl_jsonparse_get_value_as_float(&parser);
					MB_AIN_DEBUG("AIN:CFG:Hi:%s\n", uhl_flt2str(tmp_str, p_config->hi, p_ain_config->decimals));
				} else if (jsonparse_strcmp_value(&parser, "Post_type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->post_type = jsonparse_get_value_as_int(&parser);
					MB_AIN_DEBUG("AIN:CFG:Post_type:%d\n", p_config->post_type);
				} else if (jsonparse_strcmp_value(&parser, "Dec") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->decimals = jsonparse_get_value_as_int(&parser);
					MB_AIN_DEBUG("AIN:CFG:Dec:%d\n",p_config->decimals);
				}

				else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					start_cmd = (jsonparse_get_value_as_int(&parser) == 1 ? 1 : 0);
					ain_limits_notified = false;
					MB_AIN_DEBUG("AIN:Start:%d\n", start_cmd);
				}
			}
		}
		if (is_post && start_cmd != -1)
			mb_ain_timer_init(start_cmd == 1);
	}

	mb_ain_set_response(response, false, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
	
}

void ICACHE_FLASH_ATTR mb_ain_init(bool isStartReading) {
	p_ain_config = (mb_ain_config_t *)p_user_app_config_data->adc;		// set proper structure in app settings

	webserver_register_handler_callback(MB_AIN_URL, mb_ain_handler);
	device_register(NATIVE, 0, MB_AIN_DEVICE, MB_AIN_URL, NULL, NULL);
	
	if (!user_app_config_is_config_valid())
	{
		p_ain_config->autostart = MB_AIN_AUTOSTART_DEFAULT;
		p_ain_config->refresh	= MB_AIN_REFRESH_DEFAULT;
		p_ain_config->each	= MB_AIN_EACH_DEFAULT;
		p_ain_config->decimals	= 3;
		p_ain_config->threshold= MB_AIN_THRESHOLD_DEFAULT;
		p_ain_config->scale_k = MB_AIN_SCALE_K_DEFAULT;
		p_ain_config->scale_y = MB_AIN_SCALE_Y_DEFAULT;
		p_ain_config->name[0] = 0x00;
		p_ain_config->post_type = MB_AIN_POST_TYPE_DEFAULT;
		p_ain_config->action = MB_AIN_ACTION_DEFAULT;
		p_ain_config->low = 0.0f;
		p_ain_config->hi = 0.0f;
	}
	
	if (!isStartReading)
		isStartReading = (p_ain_config->autostart == 1);
	
	if (isStartReading) {
		mb_ain_timer_init(true);
	}
}
#endif
