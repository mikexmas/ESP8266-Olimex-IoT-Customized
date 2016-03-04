#include "mb_main.h"

#if DHT_ENABLE

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

#ifdef DHT_DEBUG
#undef DHT_DEBUG
#define DHT_DEBUG(...) os_printf(__VA_ARGS__);
#else
#define DHT_DEBUG(...)
#endif

LOCAL volatile uint32 dht_timer;
LOCAL bool mb_dht_sensor_fault = false;
LOCAL float mb_dht_temp, mb_dht_hum;
// store also float str: -99.99
LOCAL char mb_dht_temp_str[10];
LOCAL char mb_dht_hum_str[10];

DHT_Sensor mb_dht_sensor;
LOCAL mb_dht_config_t *p_dht_config;

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
		uhl_flt2str(tmp_str, mb_dht_temp, 2);
		strncpy_null(mb_dht_temp_str, tmp_str, 9);
		uhl_flt2str(tmp_str, mb_dht_hum, 2);
		strncpy_null(mb_dht_hum_str, tmp_str, 9);
		ret = true;

		DHT_DEBUG("DHT read OK: T: %s, H: %s\r\n", mb_dht_temp_str, mb_dht_hum_str);
	}

	return ret;
}

LOCAL void ICACHE_FLASH_ATTR mb_dht_set_response(char *response, bool is_fault, bool is_post) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_DHT_DEVICE, USER_CONFIG_USER_SIZE);
	
	DHT_DEBUG("DHT web response: hostname ? %s\n"
		"    Running: %s\n"
		"    Temp %s\n"
		"    Hum %s\n", 
		full_device_name,
		(dht_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP),
		mb_dht_temp_str,
		mb_dht_hum_str
	);
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_FAULT, NULL);
	}
	// POST request - status & config only
	else if (is_post) {
		char str_tmp1[20];
		char str_tmp2[20];
		char str_tmp3[20];
		char str_tmp4[20];		
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
					"\"Name_h\": \"%s\""
				"}",
				p_dht_config->autostart,
				p_dht_config->gpio_pin,
				p_dht_config->dht_type,
				p_dht_config->refresh,
				p_dht_config->each,
				uhl_flt2str(str_tmp1, p_dht_config->threshold_t, 2),
				uhl_flt2str(str_tmp2, p_dht_config->threshold_h, 2),
				uhl_flt2str(str_tmp3, p_dht_config->offset_t, 2),
				uhl_flt2str(str_tmp4, p_dht_config->offset_h, 2),
				p_dht_config->units,
				p_dht_config->name_t,
				p_dht_config->name_h
			)
		);

	// event: do we want special format (thingspeak) (
	} else if (user_config_events_post_format() == USER_CONFIG_EVENTS_FORMAT_THINGSPEAK) {
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%s, \"%s\":%s}",
			user_config_events_token(),
			(os_strlen(p_dht_config->name_t) == 0 ? "field1" : p_dht_config->name_t),
			mb_dht_temp_str,
			(os_strlen(p_dht_config->name_t) == 0 ? "field2" : p_dht_config->name_h),
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
	LOCAL float old_state_t = 0;
	LOCAL float old_state_h = 0;
	LOCAL uint8 count = 0;
	LOCAL uint8 errCount =0;
	char response[WEBSERVER_MAX_RESPONSE_LEN];
	char tmp_str[20];
	
	//if (dht_read()) {
	if (dht_read_from_sensor()) {
		count++;
		errCount=0;
		mb_dht_sensor_fault = false;
	} else {
		count++;
		errCount++;
	}
	
	// Check if err count; after some time we do not want to have too old value
	if (!mb_dht_sensor_fault && mb_dht_temp_str[0] != 0x00 && mb_dht_hum_str[0] != 0x00 && p_dht_config->refresh * errCount < 180) {
		if (abs(mb_dht_temp - old_state_t) > p_dht_config->threshold_t || abs(mb_dht_hum - old_state_h) > p_dht_config->threshold_h || (count >= p_dht_config->each)) {

			DHT_DEBUG("DHT: Change Temp: [%s] -> [%s], Hum: [%s] -> [%s], Count: [%d]/[%d]\n", uhl_flt2str(tmp_str, old_state_t, 2), mb_dht_temp_str, uhl_flt2str(tmp_str, old_state_h, 2), mb_dht_hum_str, p_dht_config->each, count);

			old_state_t = mb_dht_temp;
			old_state_h = mb_dht_hum;
			count = 0;
			mb_dht_set_response(response, false, false);
			user_event_raise(MB_DHT_URL, response);
		}
	}
	else {
		DHT_DEBUG("DHT: Sensor fault:%d\n", errCount);
		mb_dht_sensor_fault = true;
		mb_dht_temp_str[0] = 0x0;
		mb_dht_hum_str[0] = 0x0;
		if (count >= p_dht_config->each*10) {
			count = 0;
			mb_dht_set_response(response, true, false);
			user_event_raise(MB_DHT_URL, response);
		}
	}
}

/* Timer init */
void ICACHE_FLASH_ATTR mb_dht_timer_init(bool start_cmd){
	if (dht_timer != 0) {
		clearInterval(dht_timer);
	}
	
	if (p_dht_config->refresh == 0 || !start_cmd) {
		DHT_DEBUG("DHT Timer stopped!\n");
		dht_timer = 0;
	} else {
		DHT_DEBUG("DHT started interval: %d\n", p_dht_config->refresh);
		dht_timer = setInterval(dht_timer_update, NULL, p_dht_config->refresh*1000);
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
	
	bool is_to_store = false;
	
	mb_dht_config_t *p_config = p_dht_config;
	
	// post config for INIT
	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:CFG:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Gpio") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->gpio_pin = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:CFG:Gpio:%d\n", p_config->gpio_pin);
				} else if (jsonparse_strcmp_value(&parser, "Type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->dht_type = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:JSON:Type:%d\n", p_config->dht_type);
				} else if (jsonparse_strcmp_value(&parser, "Refresh") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->refresh = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:JSON:Refresh:%d\n", p_config->refresh);
				} else if (jsonparse_strcmp_value(&parser, "Each") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->each = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:JSON:Each:%d\n", p_config->each);
				} else if (jsonparse_strcmp_value(&parser, "Thr_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold_t = uhl_jsonparse_get_value_as_float(&parser);
					DHT_DEBUG("DHT:JSON:Thr_t:%s\n", uhl_flt2str(tmp_str, p_dht_config->threshold_t, 2));
				} else if (jsonparse_strcmp_value(&parser, "Thr_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->threshold_h = uhl_jsonparse_get_value_as_float(&parser);
					DHT_DEBUG("DHT:JSON:Thr_h:%s\n", uhl_flt2str(tmp_str, p_dht_config->threshold_h, 2));
				} else if (jsonparse_strcmp_value(&parser, "Ofs_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->offset_t = uhl_jsonparse_get_value_as_float(&parser);
					DHT_DEBUG("DHT:JSON:Ofs_t:%s\n", uhl_flt2str(tmp_str, p_dht_config->offset_t, 2));
				} else if (jsonparse_strcmp_value(&parser, "Ofs_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->offset_h = uhl_jsonparse_get_value_as_float(&parser);
					DHT_DEBUG("DHT:JSON:Ofs_h:%s\n", uhl_flt2str(tmp_str, p_dht_config->offset_h, 2));
				} else if (jsonparse_strcmp_value(&parser, "Units") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->units = jsonparse_get_value_as_int(&parser);
					DHT_DEBUG("DHT:JSON:Units:%d\n", p_config->units);
				} else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					int tmpisStart = jsonparse_get_value_as_int(&parser);
					mb_dht_timer_init(tmpisStart == 1);
					DHT_DEBUG("DHT:JSON:Start:%s\n", (dht_timer != 0 ? DEVICE_STATUS_OK : DEVICE_STATUS_STOP));
				} else if (jsonparse_strcmp_value(&parser, "Name_t") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name_t, MB_VARNAMEMAX);
					DHT_DEBUG("DHT:JSON:Name_t:%s\n", p_config->name_t);
				} else if (jsonparse_strcmp_value(&parser, "Name_h") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_copy_value(&parser, p_config->name_h, MB_VARNAMEMAX);
					DHT_DEBUG("DHT:JSON:Name_h:%s\n", p_config->name_h);
				}
			}
		}

		dht_init(&mb_dht_sensor, p_dht_config->dht_type, p_dht_config->gpio_pin);
	}
	
	mb_dht_set_response(response, false, true);
}

/* Main Initialization file
 * true: init HW & timer, else web service only (listener)
 */
void ICACHE_FLASH_ATTR mb_dht_init(bool isStartReading) {
	p_dht_config = (mb_dht_config_t *)p_user_app_config_data->dht;		// set proper structure in app settings
	
	mb_dht_temp_str[0] = 0x00;
	mb_dht_hum_str[0] = 0x00;
	
	webserver_register_handler_callback(MB_DHT_URL, mb_dht_handler);
	device_register(NATIVE, 0, MB_DHT_URL, NULL, NULL);

	
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

		isStartReading = (p_dht_config->autostart == 1);
		
		DHT_DEBUG("DHT: init with defaults!");
	}
	
	if (isStartReading) {
		dht_init(&mb_dht_sensor, p_dht_config->dht_type, p_dht_config->gpio_pin);
		mb_dht_timer_init(1);
	}
}

#endif
