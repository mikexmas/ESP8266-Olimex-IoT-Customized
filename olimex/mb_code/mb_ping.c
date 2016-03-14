#include "mb_main.h"

#if MB_PING_ENABLE

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
#include "ping/ping.h"	// we use EDAF driver

#ifdef MB_PING_DEBUG
#undef MB_PING_DEBUG
#define MB_PING_DEBUG(...) debug(__VA_ARGS__);
#else
#define MB_PING_DEBUG(...)
#endif

LOCAL volatile uint32 ping_timer;
LOCAL bool mb_ping_sensor_fault = false;
LOCAL float mb_ping_val;
// store also float str: -99.99
LOCAL char mb_ping_val_str[10];

LOCAL Ping_Data pingData;
LOCAL mb_ping_config_t *p_ping_config;

/* Reading from PING driver */
LOCAL bool ICACHE_FLASH_ATTR ping_read_from_sensor() {
	bool ret = false;
	float readVal = 0.0f;
	
	if (ping_ping(&pingData, p_ping_config->max_distance, &readVal)) {
		mb_ping_val = readVal + p_ping_config->offset;
		char tmp_str[20];
		uhl_flt2str(tmp_str, mb_ping_val, 2);
		strncpy_null(mb_ping_val_str, tmp_str, 9);
		ret = true;
        MB_PING_DEBUG("PING read OK: VAL: %s\n", mb_ping_val_str);
	}

	return ret;
}

LOCAL void ICACHE_FLASH_ATTR mb_ping_set_response(char *response, bool is_fault, bool is_post) {
	char data_str[WEBSERVER_MAX_VALUE];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_PING_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_PING_DEBUG("PING web response: configured ? %d\n"
		"    Running: %s\n"
		"    VAL %s\n", 
		1,
		(ping_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP),
		mb_ping_val_str
	);
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (is_post) {
		char str_max_dist[20];
		char str_thr[20];
		char str_ofs[20];
		uhl_flt2str(str_max_dist, p_ping_config->max_distance, 2);
		uhl_flt2str(str_thr, p_ping_config->threshold, 2);
		uhl_flt2str(str_ofs, p_ping_config->offset, 2);
		json_status(response, full_device_name, (ping_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Auto\": %d,"
					"\"Trig_pin\":%d,"
					"\"Echo_pin\": %d,"
					"\"Units\": %d,"
					"\"Refresh\": %d,"
					"\"Each\": %d,"
					"\"Max_dist\": %s,"
					"\"Thr\": %s,"
					"\"Ofs\": %s,"
					"\"Name\":\"%s\","
					"\"Post_type\":%d"
				"}",
				p_ping_config->autostart,
				p_ping_config->trigger_pin,
				p_ping_config->echo_pin,
				p_ping_config->units,
				p_ping_config->refresh,
				p_ping_config->each,
				str_max_dist,
				str_thr,
				str_ofs,
				p_ping_config->name,
				p_ping_config->post_type
			)
		);

	// event: do we want special format (thingspeak) (
	} else if (p_ping_config->post_type == MB_POSTTYPE_THINGSPEAK) {		// states change only
		json_sprintf(
			response,
			"%s{\"api_key\":\"%s\", \"%s\":%s}",
			MB_POSTTYPE_THINGSPEAK_STR,
			user_config_events_token(),
			(os_strlen(p_ping_config->name) == 0 ? "field1" : p_ping_config->name),
			mb_ping_val_str
		);
	// normal event measurement
	} else {
		json_data(
			response, full_device_name, (ping_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP), 
				json_sprintf(data_str,
					"\"ping\": {"
						"\"Value\": %s"
					"}",
					mb_ping_val_str
				),
				NULL
		);
	}
}

// timer update
void ICACHE_FLASH_ATTR ping_timer_update() {
	LOCAL float old_state = 0.0f;
	LOCAL uint8 count = 0;
	LOCAL uint8 errCount =0;
	char response[WEBSERVER_MAX_RESPONSE_LEN];
	
	if (ping_read_from_sensor()) {
		count++;
		errCount=0;
		mb_ping_sensor_fault = false;
	} else {
		count++;
		errCount++;
	}
	
	// Check if err count; after some time we do not want to have too old value
	if (!mb_ping_sensor_fault && mb_ping_val_str[0] != 0x00 && p_ping_config->refresh * errCount < 180) {
		if (abs(mb_ping_val - old_state) > p_ping_config->threshold || (count >= p_ping_config->each)) {

			MB_PING_DEBUG("PING: Change VAL: [%d] -> [%s], Count: [%d]/[%d]\n", (int)old_state, mb_ping_val_str, p_ping_config->each, count);

			old_state = mb_ping_val;
			count = 0;
			mb_ping_set_response(response, false, false);
			user_event_raise(MB_PING_URL, response);
		}
	}
	else {

		MB_PING_DEBUG("PING: Sensor fault:%d\n", errCount);

		mb_ping_sensor_fault = true;
		mb_ping_val_str[0] = 0x0;
		if (count >= p_ping_config->each*10) {
			count = 0;
			mb_ping_set_response(response, true, false);
			user_event_raise(MB_PING_URL, response);
		}
	}
}

/* Timer init */
void ICACHE_FLASH_ATTR mb_ping_timer_init(bool start_cmd){
	if (ping_timer != 0) {
		clearInterval(ping_timer);
	}
	
	if (p_ping_config->refresh == 0 || !start_cmd) {
		MB_PING_DEBUG("PING Timer not start!\n");
		ping_timer = 0;
	} else {
		MB_PING_DEBUG("PING setInterval: %d\n", p_ping_config->refresh);
		ping_timer = setInterval(ping_timer_update, NULL, p_ping_config->refresh*1000);
	}
}

void ICACHE_FLASH_ATTR mb_ping_handler(
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
	
	mb_ping_config_t *p_config = p_ping_config;
	bool is_post = (method == POST);
	int start_cmd = -1;	// 0=STOP, 1=START other none
	
	// post config for INIT
	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Trig_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->trigger_pin = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Trig_pin:%d\n", p_config->trigger_pin);
				} else if (jsonparse_strcmp_value(&parser, "Echo_pin") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->echo_pin = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Echo_pin:%d\n", p_config->echo_pin);
				} else if (jsonparse_strcmp_value(&parser, "Units") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->units = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Units:%d\n", p_config->units);
				} else if (jsonparse_strcmp_value(&parser, "Refresh") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->refresh = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Refresh:%d\n", p_config->refresh);
				} else if (jsonparse_strcmp_value(&parser, "Each") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					MB_PING_DEBUG("PING:JSON:Each: %d\n", p_config->each);
				} else if (jsonparse_strcmp_value(&parser, "Max_dist") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->max_distance = uhl_jsonparse_get_value_as_float(&parser);
					MB_PING_DEBUG("PING:JSON:Max_dist:%s\n", uhl_flt2str(tmp_str, p_ping_config->max_distance, 2));
				} else if (jsonparse_strcmp_value(&parser, "Thr") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold = uhl_jsonparse_get_value_as_float(&parser);
					MB_PING_DEBUG("PING:JSON:Thr:%s\n", uhl_flt2str(tmp_str, p_ping_config->threshold, 2));
				} else if (jsonparse_strcmp_value(&parser, "Ofs") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->offset = uhl_jsonparse_get_value_as_float(&parser);
					MB_PING_DEBUG("PING:JSON:Ofs:%s\n", uhl_flt2str(tmp_str, p_ping_config->offset, 2));
				} else if (jsonparse_strcmp_value(&parser, "Name") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name, MB_VARNAMEMAX);
					MB_PING_DEBUG("PING:JSON:Name:%s\n", p_config->name);
				} else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					start_cmd = (jsonparse_get_value_as_int(&parser) == 1 ? 1 : 0);
					MB_PING_DEBUG("PING:JSON:Start:%d\n", start_cmd);
				}
			}
		}

		if (is_post) {
			ping_init(&pingData, p_ping_config->trigger_pin, p_ping_config->echo_pin, p_ping_config->units);
			if (start_cmd != -1)
				mb_ping_timer_init(start_cmd == 1);
		}
	}
	
	mb_ping_set_response(response, false, is_post);
}

/* Main Initialization file
 * true: init HW & timer, else web service only (listener)
 */
void ICACHE_FLASH_ATTR mb_ping_init(bool isStartReading) {
	p_ping_config = (mb_ping_config_t *)p_user_app_config_data->ping;		// set proper structure in app settings
	
	mb_ping_val_str[0] = 0x00;	// force blank string at start
	
	webserver_register_handler_callback(MB_PING_URL, mb_ping_handler);
	device_register(NATIVE, 0, MB_PING_URL, NULL, NULL);
	
	if (!user_app_config_is_config_valid())
	{
		p_ping_config->autostart = MB_PING_AUTOSTART;
		p_ping_config->trigger_pin = MB_PING_TRIGGER_PIN_DEFAULT;
		p_ping_config->echo_pin = MB_PING_ECHO_PIN_DEFAULT;
		p_ping_config->units = MB_PING_UNITS_DEFAULT;
		p_ping_config->refresh	= MB_PING_REFRESH_DEFAULT;
		p_ping_config->each	= MB_PING_EACH_DEFAULT;
		p_ping_config->max_distance= MB_PING_MAXDISTANCE_DEFAULT;
		p_ping_config->threshold= MB_PING_THRESHOLD_DEFAULT;
		p_ping_config->offset = MB_PING_OFFSET_DEFAULT;
		p_ping_config->name[0] = 0x00;;

		isStartReading = (p_ping_config->autostart == 1);
	}
	
	if (isStartReading) {
		ping_init(&pingData, p_ping_config->trigger_pin, p_ping_config->echo_pin, p_ping_config->units);
		mb_ping_timer_init(true);
	}
}

#endif
