#include "mb_main.h"

#if MB_DHT_ENABLE

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
#include "dhtxx/dhtxx.h"	// we use EDAF DHTxx driver

#ifdef MB_DHT_DEBUG
#undef MB_DHT_DEBUG
#define MB_DHT_DEBUG(...) os_printf(__VA_ARGS__);
#else
#define MB_DHT_DEBUG(...)
#endif

// OUT, T-HI, T-LOW, H-HI, L-LOW, T-IN, H-IN
const char MB_DHT_LIMITS_NONE[] = "";
const char MB_DHT_LIMITS_T_HI[] = "T-HI";
const char MB_DHT_LIMITS_T_LOW[] = "T-LOW";
const char MB_DHT_LIMITS_H_HI[] = "H-HI";
const char MB_DHT_LIMITS_H_LOW[] = "H-LOW";
const char MB_DHT_LIMITS_T_IN[] = "T-IN";
const char MB_DHT_LIMITS_H_IN[] = "H-IN";

LOCAL volatile uint32 dht_timer;
LOCAL bool mb_dht_sensor_fault = false;
LOCAL uint32 mb_limits_notified_t = MB_LIMITS_NOTIFY_INIT;
LOCAL uint32 mb_limits_notified_h = MB_LIMITS_NOTIFY_INIT;
LOCAL char *mb_limits_notified_t_str = (char*)MB_DHT_LIMITS_NONE;
LOCAL char *mb_limits_notified_h_str = (char*)MB_DHT_LIMITS_NONE;
LOCAL float mb_dht_temp, mb_dht_hum;
// store also float str: -99.99
LOCAL char mb_dht_temp_str[10];
LOCAL char mb_dht_hum_str[10];

LOCAL DHT_Sensor mb_dht_sensor;				// driver config
LOCAL mb_dht_config_t *p_dht_config;

#if MB_ACTIONS_ENABLE
mb_action_data_t mb_dht_action_data;
#endif

/* Reading from DHT11/22 */
LOCAL bool ICACHE_FLASH_ATTR dht_read_from_sensor() {
	bool ret = false;
	
	DHT_Sensor_Output current_readings;
	if (dht_read(&mb_dht_sensor, &current_readings)) {
		mb_dht_hum = current_readings.humidity;
		if (p_dht_config->units == 1) {
			mb_dht_temp = uhl_convert_c_to_f(current_readings.temperature);
		}
		else {
			mb_dht_temp = current_readings.temperature;
		}
		mb_dht_temp = mb_dht_temp + p_dht_config->offset_t;
		mb_dht_hum = mb_dht_hum + p_dht_config->offset_h;
		char tmp_str[20];
		uhl_flt2str(tmp_str, mb_dht_temp, MB_DHT_DECIMALS);
		strncpy_null(mb_dht_temp_str, tmp_str, 9);
		uhl_flt2str(tmp_str, mb_dht_hum, MB_DHT_DECIMALS);
		strncpy_null(mb_dht_hum_str, tmp_str, 9);
		ret = true;

		MB_DHT_DEBUG("DHT:Read:OK:T:%s,H:%s\n", mb_dht_temp_str, mb_dht_hum_str);
	}
	else {
		MB_DHT_DEBUG("DHT:Read:ERROR\n");
	}

	return ret;
}

/* Evaluate if and which event triggered IFTTT: OUT, T-HI, T-LOW, H-HI, L-LOW, T-IN, H-IN */
// ret: 0 nothing, 0x01=THI, 0x02= TLOW, IN=0x03, H=0x10....
LOCAL uint8 ICACHE_FLASH_ATTR mb_dht_which_event(char **p_str_t, char **p_str_h) {
	uint8 ret = 0x00;
	*p_str_t = (char*)MB_DHT_LIMITS_NONE;
	*p_str_h = (char*)MB_DHT_LIMITS_NONE;
	
	// T
	if (uhl_fabs(p_dht_config->low_t - p_dht_config->hi_t) > 1.0f) {
		if (mb_dht_temp > p_dht_config->hi_t) {
			ret = MB_LIMITS_NOTIFY_HI;
			*p_str_t = (char*)MB_DHT_LIMITS_T_HI;
		} else if (mb_dht_temp < p_dht_config->low_t) {
			ret = MB_LIMITS_NOTIFY_LOW;
			*p_str_t = (char*)MB_DHT_LIMITS_T_LOW;
		} else if ((mb_dht_temp > p_dht_config->low_t + p_dht_config->threshold_t) && (mb_dht_temp < p_dht_config->hi_t -  p_dht_config->threshold_t)) {
			ret = MB_LIMITS_NOTIFY_IN;
			*p_str_t = (char*)MB_DHT_LIMITS_T_IN;
		}
	}
	// H
	if (uhl_fabs(p_dht_config->low_h - p_dht_config->hi_h) > 1.0f) {
		if (mb_dht_hum > p_dht_config->hi_h) {
			ret = MB_LIMITS_NOTIFY_HI << 4 & ret;
			*p_str_h = (char*)MB_DHT_LIMITS_H_HI;
		} else if (mb_dht_hum < p_dht_config->low_h) {
			ret = MB_LIMITS_NOTIFY_LOW << 4 & ret;
			*p_str_h = (char*)MB_DHT_LIMITS_H_LOW;
		} else if ((mb_dht_hum > p_dht_config->low_h + p_dht_config->threshold_h) && (mb_dht_hum < p_dht_config->hi_h -  p_dht_config->threshold_h)) {
			ret = MB_LIMITS_NOTIFY_IN << 4 & ret;
			*p_str_h = (char*)MB_DHT_LIMITS_H_IN;
		}
	}

	return ret;
}

// req_type: GET/POST/or SPECIAL=IFTTT/THINGSPEAK
LOCAL void ICACHE_FLASH_ATTR mb_dht_set_response(char *response, bool is_fault, uint8 req_type) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_DHT_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_DHT_DEBUG("DHT:Resp.prep:%d;isFault:%d\n",req_type, is_fault);
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (req_type==MB_REQTYPE_POST) {
		char str_tmp1[15];
		char str_tmp2[15];
		char str_tmp3[15];
		char str_tmp4[15];
		char str_tmp5[15];
		char str_tmp6[15];
		char str_tmp7[15];
		char str_tmp8[15];
		json_status(response, full_device_name, (dht_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Auto\": %d,"
					"\"Gpio\":%d,"
					"\"Type\": %d,"
					"\"Refresh\": %d,"
					"\"Each\": %d,"
					"\"Thr_t\": %s,"
					"\"Thr_h\": %s,"
					"\"Ofs_t\": %s,"
					"\"Ofs_h\": %s,"
					"\"Units\": %d,"
					"\"Name_t\": \"%s\","
					"\"Name_h\": \"%s\","
					"\"Post_type\":%d,"
					"\"Action\":%d,"
					"\"Low_t\": %s,"
					"\"Hi_t\": %s,"
					"\"Low_h\": %s,"
					"\"Hi_h\": %s"
				"}",
				p_dht_config->autostart,
				p_dht_config->gpio_pin,
				p_dht_config->dht_type,
				p_dht_config->refresh,
				p_dht_config->each,
				uhl_flt2str(str_tmp1, p_dht_config->threshold_t, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp2, p_dht_config->threshold_h, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp3, p_dht_config->offset_t, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp4, p_dht_config->offset_h, MB_DHT_DECIMALS),
				p_dht_config->units,
				p_dht_config->name_t,
				p_dht_config->name_h,
				p_dht_config->post_type,
				p_dht_config->action,
				uhl_flt2str(str_tmp5, p_dht_config->low_t, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp6, p_dht_config->hi_t, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp7, p_dht_config->low_h, MB_DHT_DECIMALS),
				uhl_flt2str(str_tmp8, p_dht_config->hi_h, MB_DHT_DECIMALS)
			)
		);

	// event: thingspeak (
	} else if (req_type==MB_REQTYPE_SPECIAL && p_dht_config->post_type == MB_POSTTYPE_THINGSPEAK) {
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%s, \"%s\":%s}",
			user_config_events_token(),
			(os_strlen(p_dht_config->name_t) == 0 ? "field1" : p_dht_config->name_t),
			mb_dht_temp_str,
			(os_strlen(p_dht_config->name_h) == 0 ? "field2" : p_dht_config->name_h),
			mb_dht_hum_str
		);

	// event: IFTTT; measurement is evaluated before
	} else if (req_type==MB_REQTYPE_SPECIAL && p_dht_config->post_type == MB_POSTTYPE_IFTTT) {
		char signal_name[30];
		signal_name[0] = 0x00;
		os_sprintf(signal_name, "%s[%s]/%s[%s]",
			(os_strlen(p_dht_config->name_t) == 0 ? "T" : p_dht_config->name_t),
			mb_limits_notified_t_str,
			(os_strlen(p_dht_config->name_h) == 0 ? "H" : p_dht_config->name_h),
			mb_limits_notified_h_str);
		json_sprintf(
			response,
			"{\"value1\":\"%s\",\"value2\":\"%s\", \"value3\": \"%s\"}",
			signal_name,
			mb_dht_temp_str,
			mb_dht_hum_str
		);

	// normal event measurement
	} else {
		json_data(
			response, full_device_name, (dht_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
				json_sprintf(data_str,
					"\"dht\": {"
						"\"Temperature\": %s,"
						"\"Humidity\": %s"
					"}",
					mb_dht_temp_str,
					mb_dht_hum_str
				),
				NULL
		);
	}
}

// timer update
void ICACHE_FLASH_ATTR dht_timer_update() {
	LOCAL float old_state_t = -999.999f;
	LOCAL float old_state_h = -999.999f;
	LOCAL uint8 count = 0;
	LOCAL uint8 errCount = 0;
	char response[WEBSERVER_MAX_RESPONSE_LEN];
	char tmp_str[20];
	char tmp_str1[20];
	
	//if (dht_read()) {
	if (dht_read_from_sensor()) {
		count++;
		errCount=0;
		mb_dht_sensor_fault = false;
	} else {
		errCount++;
	}
	
	// Check if err count; after some time we do not want to have too old value
	if (!mb_dht_sensor_fault && mb_dht_temp_str[0] != 0x00 && mb_dht_hum_str[0] != 0x00 && p_dht_config->refresh * errCount < MB_DHT_ERROR_COUNT * 1000) {
	
		// Evaluate limits in some cases; we need later for eg IFTTT / internal action
		if (p_dht_config->post_type == MB_POSTTYPE_IFTTT
#if MB_ACTIONS_ENABLE
			|| p_dht_config->action >= MB_ACTIONTYPE_FIRST && p_dht_config->action <= MB_ACTIONTYPE_LAST
#endif
				) {
			uint8 eval_val = 0x00;
			uint8 tmp_event_notified_t;
			uint8 tmp_event_notified_h;
			bool make_event = false;


			eval_val = mb_dht_which_event(&mb_limits_notified_t_str, &mb_limits_notified_h_str);
			tmp_event_notified_t = eval_val & 0x0F;
			tmp_event_notified_h = eval_val & 0xF0 >> 4;
			make_event = false;
				
			if (mb_limits_notified_t != tmp_event_notified_t && tmp_event_notified_t != MB_LIMITS_NOTIFY_INIT)		// T
			{
				make_event = true;
				mb_limits_notified_t = tmp_event_notified_t;
			}
				
			if (mb_limits_notified_h != tmp_event_notified_h && tmp_event_notified_h != MB_LIMITS_NOTIFY_INIT) 		// H
			{
				make_event = true;
				mb_limits_notified_h = tmp_event_notified_h;
			}
			
			MB_DHT_DEBUG("DHT:Eval:%d,Notif_T:%d,Notif_T_str:%s,Notif_H:%d,Notif_H_str:%s,Make:%d\n",eval_val, mb_limits_notified_t, mb_limits_notified_t_str, mb_limits_notified_h, mb_limits_notified_h_str, make_event);
			
			// Special handling; notify once only when limit exceeded
			if (make_event && p_dht_config->post_type == MB_POSTTYPE_IFTTT) {	// IFTTT limits check; make hysteresis to reset flag
				mb_dht_set_response(response, false, MB_REQTYPE_SPECIAL);
				webclient_post(user_config_events_ssl(), user_config_events_user(), user_config_events_password(), user_config_events_server(), user_config_events_ssl() ? WEBSERVER_SSL_PORT : WEBSERVER_PORT, user_config_events_path(), response);
			}
#if MB_ACTIONS_ENABLE			
			if (make_event && p_dht_config->action >= MB_ACTIONTYPE_FIRST && p_dht_config->action <= MB_ACTIONTYPE_LAST) {	// ACTION: DIO
				mb_dht_action_data.action_type = p_dht_config->action;
				if (mb_limits_notified_t == MB_LIMITS_NOTIFY_HI || mb_limits_notified_t == MB_LIMITS_NOTIFY_LOW
						|| mb_limits_notified_h == MB_LIMITS_NOTIFY_HI || mb_limits_notified_h == MB_LIMITS_NOTIFY_LOW)
					mb_dht_action_data.value = 1;
				else 
					mb_dht_action_data.value = 0;
				setTimeout(mb_action_post, &mb_dht_action_data, 10);
			}
#endif
		}
	
		// Evaluate change of measurement value
		if (uhl_fabs(mb_dht_temp - old_state_t) > p_dht_config->threshold_t
				|| uhl_fabs(mb_dht_hum - old_state_h) > p_dht_config->threshold_h 
				|| (count >= p_dht_config->each && (uhl_fabs(mb_dht_temp - old_state_t) > 0.1f || uhl_fabs(mb_dht_hum - old_state_h) > 0.1f))
				|| (count >= 0xFF)	// count is 8bits, allways make after ff
			) {

			MB_DHT_DEBUG("DHT: Change Temp: [%s] -> [%s], Hum: [%s] -> [%s], Count: [%d]/[%d]\n", uhl_flt2str(tmp_str, old_state_t, MB_DHT_DECIMALS), mb_dht_temp_str, uhl_flt2str(tmp_str1, old_state_h, MB_DHT_DECIMALS), mb_dht_hum_str, p_dht_config->each, count);

			old_state_t = mb_dht_temp;
			old_state_h = mb_dht_hum;
			count = 0;

			// Thinspeak 
			if (p_dht_config->post_type == MB_POSTTYPE_THINGSPEAK) {
				mb_dht_set_response(response, false, MB_REQTYPE_SPECIAL);	
				webclient_post(user_config_events_ssl(), user_config_events_user(), user_config_events_password(), user_config_events_server(), user_config_events_ssl() ? WEBSERVER_SSL_PORT : WEBSERVER_PORT, user_config_events_path(), response);
			}

			// Standard event - send anyway,ifttt/ts is additional sent before
			mb_dht_set_response(response, false, MB_REQTYPE_NONE);
			user_event_raise(MB_DHT_URL, response);
		}
	}
	else if (!mb_dht_sensor_fault && p_dht_config->refresh * errCount >= MB_DHT_ERROR_COUNT *1000) {	// first time only sensor fault: send notification
		MB_DHT_DEBUG("DHT: Sensor fault:%d\n", errCount);
		mb_dht_sensor_fault = true;
		mb_dht_temp_str[0] = 0x0;
		mb_dht_hum_str[0] = 0x0;
		old_state_t = -999.99f;		// set to enable instant sending
		old_state_h = -999.99f;
		errCount = 0;
		mb_dht_set_response(response, true, MB_REQTYPE_NONE);
		user_event_raise(MB_DHT_URL, response);
	} else if (mb_dht_sensor_fault && errCount >= 0xff) {		// notify sensor fault occasionally on 255x read
		errCount = 0;
		mb_dht_set_response(response, true, MB_REQTYPE_NONE);
		user_event_raise(MB_DHT_URL, response);
	}
}

/* Timer init */
void ICACHE_FLASH_ATTR mb_dht_timer_init(bool start_cmd) {
	if (dht_timer != 0) {
		clearInterval(dht_timer);
	}
	
	if (p_dht_config->refresh == 0 || !start_cmd) {
		MB_DHT_DEBUG("DHT:Timer:Stopped!\n");
		dht_timer = 0;
	} else {
		MB_DHT_DEBUG("DHT:Timer:Started:%d\n", p_dht_config->refresh);
		dht_timer = setInterval(dht_timer_update, NULL, p_dht_config->refresh*1000);
		
		mb_limits_notified_t = MB_LIMITS_NOTIFY_INIT;		// IFTTT notification flag reset
		mb_limits_notified_h = MB_LIMITS_NOTIFY_INIT;		// IFTTT notification flag reset
		mb_limits_notified_t_str = (char*)MB_DHT_LIMITS_NONE;
		mb_limits_notified_h_str = (char*)MB_DHT_LIMITS_NONE;
		dht_timer_update();									// instant measurement, otherwised we have to wait timer elapsed
	}
}

void ICACHE_FLASH_ATTR mb_dht_handler(
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
	
	mb_dht_config_t *p_config = p_dht_config;
	
	bool is_post = (method == POST);	// it means POST config data
	int start_cmd = -1;
	uint8 tmp_ret = 0xFF;
	
	// post config for INIT
	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Gpio") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->gpio_pin = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Gpio:%d\n", p_config->gpio_pin);
				} else if (jsonparse_strcmp_value(&parser, "Type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->dht_type = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Type:%d\n", p_config->dht_type);
				} else if (jsonparse_strcmp_value(&parser, "Refresh") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->refresh = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Refresh:%d\n", p_config->refresh);
				} else if (jsonparse_strcmp_value(&parser, "Each") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Each:%d\n", p_config->each);
				} else if (jsonparse_strcmp_value(&parser, "Thr_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold_t = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Thr_t:%s\n", uhl_flt2str(tmp_str, p_dht_config->threshold_t, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Thr_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold_h = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Thr_h:%s\n", uhl_flt2str(tmp_str, p_dht_config->threshold_h, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Ofs_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->offset_t = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Ofs_t:%s\n", uhl_flt2str(tmp_str, p_dht_config->offset_t, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Ofs_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->offset_h = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Ofs_h:%s\n", uhl_flt2str(tmp_str, p_dht_config->offset_h, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Units") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->units = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Units:%d\n", p_config->units);
				} else if (jsonparse_strcmp_value(&parser, "Name_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name_t, MB_VARNAMEMAX);
					MB_DHT_DEBUG("DHT:CFG:Name_t:%s\n", p_config->name_t);
				} else if (jsonparse_strcmp_value(&parser, "Name_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name_h, MB_VARNAMEMAX);
					MB_DHT_DEBUG("DHT:CFG:Name_h:%s\n", p_config->name_h);
				} else if (jsonparse_strcmp_value(&parser, "Low_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->low_t = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Low_t:%s\n", uhl_flt2str(tmp_str, p_config->low_t, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Hi_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->hi_t = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Hi_t:%s\n", uhl_flt2str(tmp_str, p_config->hi_t, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Low_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->low_h = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Low_h:%s\n", uhl_flt2str(tmp_str, p_config->low_h, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Hi_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->hi_h = uhl_jsonparse_get_value_as_float(&parser);
					MB_DHT_DEBUG("DHT:CFG:Hi_h:%s\n", uhl_flt2str(tmp_str, p_config->hi_h, MB_DHT_DECIMALS));
				} else if (jsonparse_strcmp_value(&parser, "Post_type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->post_type = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Post_type:%d\n", p_config->post_type);
				} else if (jsonparse_strcmp_value(&parser, "Action") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->action = jsonparse_get_value_as_int(&parser);
					MB_DHT_DEBUG("DHT:CFG:Action:%d\n", p_config->action);
				}
				// Not config params
				else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					start_cmd = (jsonparse_get_value_as_int(&parser) == 1 ? 1 : 0);
					MB_DHT_DEBUG("DHT:CFG:Start:%d\n", start_cmd);
				} else if (tmp_ret = user_app_config_handler_part(&parser) != 0xFF){	// check for common app commands
					MB_DHT_DEBUG("DHT:CFG:APPCONFIG:%d\n", tmp_ret);
				}
			}
		}
	
		if (is_post) {
			dht_init(&mb_dht_sensor, p_dht_config->dht_type, p_dht_config->gpio_pin);
			if (start_cmd != -1)
				mb_dht_timer_init(start_cmd==1);
		}
	}
	
	mb_dht_set_response(response, false, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
}

/* Main Initialization file
 * true: init HW & timer, else web service only (listener)
 */
void ICACHE_FLASH_ATTR mb_dht_init(bool isStartReading) {
	p_dht_config = (mb_dht_config_t *)p_user_app_config_data->dht;		// set proper structure in app settings
	
	mb_dht_temp_str[0] = 0x00;
	mb_dht_hum_str[0] = 0x00;
	
	webserver_register_handler_callback(MB_DHT_URL, mb_dht_handler);
	device_register(NATIVE, 0, MB_DHT_DEVICE, MB_DHT_URL, NULL, NULL);

	if (!user_app_config_is_config_valid())
	{
		p_dht_config->autostart = MB_DHT_AUTOSTART;
		p_dht_config->gpio_pin = MB_DHT_GPIO_PIN_DEFAULT;
		p_dht_config->dht_type = MB_DHT_TYPE_DEFAULT;
		p_dht_config->refresh	= MB_DHT_REFRESH_DEFAULT;
		p_dht_config->each	= MB_DHT_EACH_DEFAULT;
		p_dht_config->threshold_t= MB_DHT_T_THRESHOLD_DEFAULT;
		p_dht_config->threshold_h = MB_DHT_H_THRESHOLD_DEFAULT;
		p_dht_config->offset_t = MB_DHT_T_OFFSET_DEFAULT;
		p_dht_config->offset_h = MB_DHT_H_OFFSET_DEFAULT;
		p_dht_config->units = MB_DHT_UNITS_DEFAULT;
		p_dht_config->name_t[0] = 0x00;
		p_dht_config->name_h[0] = 0x00;
		p_dht_config->post_type = MB_POSTTYPE_DEFAULT;
		p_dht_config->action = MB_ACTIONTYPE_NONE;
		p_dht_config->low_t = 0.0f;
		p_dht_config->hi_t = 0.0f;
		p_dht_config->low_h = 0.0f;
		p_dht_config->hi_h = 0.0f;
		
		MB_DHT_DEBUG("DHT: init with defaults!");
	}
	
	if (!isStartReading)
		isStartReading = (p_dht_config->autostart == 1);	
	
	if (isStartReading) {
		dht_init(&mb_dht_sensor, p_dht_config->dht_type, p_dht_config->gpio_pin);
		mb_dht_timer_init(1);
	}
}

#endif
