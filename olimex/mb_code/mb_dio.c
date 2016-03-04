#include "mb_main.h"

#if MB_DIO_ENABLE

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

#include "easygpio/easygpio.h"
#include "mb_app_config.h"
#include "mb_helper_library.h"

#ifdef MB_DIO_DEBUG
#undef MB_DIO_DEBUG
#define MB_DIO_DEBUG(...) os_printf(__VA_ARGS__);
#else
#define MB_DIO_DEBUG(...)
#endif

LOCAL mb_dio_config_t *p_dio_config = NULL;
LOCAL mb_dio_work_t dio_work[MB_DIO_ITEMS];
LOCAL volatile uint32_t dio_all_inputs = 0; // a mask containing all of the initiated interrupt pins

// forward declarations
LOCAL void mb_dio_disableInterrupt(int8_t pin);
LOCAL void mb_dio_intr_handler(void *arg);
LOCAL void mb_dio_set_response(char *response, mb_dio_work_t *p_work, bool is_post);
LOCAL void mb_dio_set_output(mb_dio_work_t *p_work);

LOCAL void
mb_dio_disableInterrupt(int8_t pin) {
  if (pin>=0){
    gpio_pin_intr_state_set(GPIO_ID_PIN(pin), GPIO_PIN_INTR_DISABLE);
  }
}

LOCAL void ICACHE_FLASH_ATTR mb_dio_intr_timer(mb_dio_work_t *p_work) {
	os_timer_disarm(&p_work->timer);
	char response[WEBSERVER_MAX_RESPONSE_LEN];
	
	p_work->state = GPIO_INPUT_GET(p_work->p_config->gpio_pin);
	if (p_work->p_config->inverse) {
		p_work->state = !p_work->state;
	}
	
	MB_DIO_DEBUG("DIO:Intr:Timer:Index:%d,Gpio:%d,State:%d\n", p_work->index, p_work->p_config->gpio_pin, p_work->state);
	
	// Check new state => event; when edge detection => allways send
	if ((p_work->p_config->type >= DIO_IN_PU_POS || p_work->p_config->type <= DIO_IN_NP_NEG) || (p_work->state != p_work->state_old)) {
		p_work->state_old = p_work->state;
		mb_dio_set_response(response, p_work, false);
		user_event_raise(MB_DIO_URL, response);
	}
}

LOCAL void
mb_dio_intr_handler(void *arg) {
	uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
  
	int i=0;
	for (i;i<MB_DIO_ITEMS;i++) {
		mb_dio_work_t *p_cur_work = &dio_work[i];
		if (p_cur_work->p_config != NULL && p_cur_work->p_config->type >=DIO_IN_NOPULL && p_cur_work->p_config->type <=19 &&  p_cur_work->p_config->gpio_pin >= 0 && (gpio_status & BIT(p_cur_work->p_config->gpio_pin))) {
			GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(p_cur_work->p_config->gpio_pin));

			os_timer_disarm(&p_cur_work->timer);
			os_timer_setfn(&p_cur_work->timer, (os_timer_func_t *)mb_dio_intr_timer, p_cur_work);
			os_timer_arm(&p_cur_work->timer, MB_DIO_FLT_TOUT, 0);
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR mb_dio_output_timer(mb_dio_work_t *p_work) {
	if (p_work != NULL && p_work->p_config != NULL) {
		os_timer_disarm(&p_work->timer);
		p_work->timer_run = 0;
		//MB_DIO_DEBUG("DIO:OUTPUT_Timer222:Id:%d,State:%d,StateOld:%d\n", p_work->index, p_work->state, p_work->state_old);
		p_work->state_old = p_work->state;
		p_work->state = !p_work->state;
		mb_dio_set_output(p_work);
	}	
}

LOCAL void ICACHE_FLASH_ATTR mb_dio_set_output(mb_dio_work_t *p_work) {
	if (p_work != NULL && p_work->p_config != NULL) {
		uint8 tmp_state = (p_work->p_config->inverse ? !p_work->state : p_work->state);
		GPIO_OUTPUT_SET(p_work->p_config->gpio_pin, tmp_state);
		MB_DIO_DEBUG("DIO:OutSet:Index:%d,Gpio:%d,State:%d,StateOld:%d,PhyPin:%d\n", p_work->index, p_work->p_config->gpio_pin, p_work->state, p_work->state_old, tmp_state);

		if (!p_work->state_old && p_work->state && p_work->p_config->pls_on != 0) {
			if (!p_work->timer_run) {
				//clearTimeout(p_work->timer_pls);
				//p_work->timer_pls = setTimeout(mb_dio_output_timer, p_work, p_work->p_config->pls_on);
				os_timer_disarm(&p_work->timer);
				os_timer_setfn(&p_work->timer, (os_timer_func_t *)mb_dio_output_timer, p_work);
				os_timer_arm(&p_work->timer, p_work->p_config->pls_on, 0);
				p_work->timer_run = 1;
			}
		} else if (p_work->state_old && !p_work->state && p_work->p_config->pls_off != 0) {
			if (!p_work->timer_run) {
				//clearTimeout(p_work->timer_pls);
				//p_work->timer_pls = setTimeout(mb_dio_output_timer, p_work, p_work->p_config->pls_off);
				os_timer_disarm(&p_work->timer);
				os_timer_setfn(&p_work->timer, (os_timer_func_t *)mb_dio_output_timer, p_work);
				os_timer_arm(&p_work->timer, p_work->p_config->pls_off, 0);
				p_work->timer_run = 1;
			}
		}

		/*
		char response[WEBSERVER_MAX_RESPONSE_LEN];
		p_work->state_old = p_work->state;
		mb_dio_set_response(response, p_work, false);
		user_event_raise(MB_DIO_URL, response);
		*/
	}
	
	return;
}

LOCAL void ICACHE_FLASH_ATTR mb_dio_set_response(char *response, mb_dio_work_t *p_work, bool is_post) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_DIO_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_DIO_DEBUG("DIO web response: hostname ? %s\n"
		"    Running: %s\n"
		"    DIO Gpio:%d\n"
		"    State:%d\n", 
		full_device_name,
		"",
		(p_work != NULL ? p_work->p_config->gpio_pin : -1),
		(p_work != NULL ? p_work->state : -1)
	);
	
	// POST request - status & config only
	if (is_post) {
		int i=0;
		char str_tmp[256];
		str_tmp[0] = 0x00;
		for (i;i<MB_DIO_ITEMS;i++) {
			mb_dio_work_t *p_cur_work = &dio_work[i];
			if (p_cur_work->p_config != NULL) {
				char tmp_1[48];
				mb_dio_config_item_t *p_cur_config = p_cur_work->p_config;
				os_sprintf(tmp_1, ", \"Dio%d\":{\"Gpio\": %d, \"Type\":%d, \"Init\": %d, \"Inv\": %d, \"Pls_on\": %d, \"Pls_off\": %d}", i, p_cur_config->gpio_pin, p_cur_config->type, p_cur_config->init_state, p_cur_config->inverse, p_cur_config->pls_on, p_cur_config->pls_off);
				os_strcat(str_tmp, tmp_1);
			}
		}

		json_status(response, full_device_name, DEVICE_STATUS_OK, 
			json_sprintf(
				data_str, 
				"\"Config\" : {"
					"\"Auto\":%d"
					"%s"
				"}",
				p_dio_config->autostart,
				str_tmp
			)
		);

	// event: do we want special format (thingspeak) (
	} else if (user_config_events_post_format() == USER_CONFIG_EVENTS_FORMAT_THINGSPEAK && p_work != NULL) {		// states change only
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%s}",
			user_config_events_token(),
			(os_strlen(p_work->p_config->name) == 0 ? "field1" : p_work->p_config->name),
			p_work->state
		);
	// normal event measurement
	} else {
		// prepare string
		char str_tmp[256];
		str_tmp[0] = 0x00;
		// if item is given, just send update for this, otherwise for all
		if (p_work == NULL) {
			int i=0;
			for (i; i<MB_DIO_ITEMS; i++) {
				mb_dio_work_t *p_cur_work = &dio_work[i];
				if (p_cur_work->p_config != NULL) {
					char tmp_1[48];
					mb_dio_config_item_t *p_cur_config = p_cur_work->p_config;
					if (p_cur_config->type >= DIO_IN_NOPULL && p_cur_config->type <= __DIO_IN_LAST) {
						os_sprintf(tmp_1, "%s\"Input%d\": %d", (str_tmp[0] == 0x00 ? "" : ","), i, p_cur_work->state);
					} else if (p_cur_config->type >= DIO_OUT && p_cur_config->type <= __DIO_LAST) {
						os_sprintf(tmp_1, "%s\"Output%d\": %d", (str_tmp[0] == 0x00 ? "" : ","), i, p_cur_work->state);
					}
					os_strcat(str_tmp, tmp_1);
				}
			}
		} else if (p_work->p_config != NULL) {
			mb_dio_config_item_t *p_cur_config = p_work->p_config;
			if (p_cur_config->type >= DIO_IN_NOPULL && p_cur_config->type <= __DIO_IN_LAST) {
				os_sprintf(str_tmp, "\"Input%d\": %d", p_work->index, p_work->state);
			} else if (p_cur_config->type >= DIO_OUT && p_cur_config->type <= __DIO_OUT_LAST) {
				os_sprintf(str_tmp, "\"Output%d\": %d", p_work->index, p_work->state);
			}
		}

		json_data(
			response, full_device_name, DEVICE_STATUS_OK,
				json_sprintf(data_str,
					"\"DIO\": {"
						"%s"
					"}",
					str_tmp
				),
				NULL
		);
	}
}

LOCAL bool ICACHE_FLASH_ATTR mb_dio_hw_init(int index) {
	bool rv = false;
	mb_dio_config_item_t *p_cur_config = &p_dio_config->items[index];
	mb_dio_work_t *p_cur_work = &dio_work[index];
	p_cur_work->p_config = NULL;
	
	int pin = p_cur_config->gpio_pin;
	EasyGPIO_PullStatus pin_stat = -1;
	EasyGPIO_PinMode pin_mode = 0;
	GPIO_INT_TYPE pin_trig = GPIO_PIN_INTR_ANYEDGE;	// input trigger
	switch (p_cur_config->type) {
	case DIO_IN_NOPULL:
		pin_stat = EASYGPIO_NOPULL;
		pin_mode = EASYGPIO_INPUT;
		break;
	case DIO_IN_PULLUP:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_INPUT;
		break;
	case DIO_IN_PU_POS:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_INPUT;
		pin_trig = GPIO_PIN_INTR_POSEDGE;
		break;
	case DIO_OUT:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_OUTPUT;
		break;
	}
	
	MB_DIO_DEBUG("DIO:INIT:1:%d,%d,%d\n", pin, pin_stat, pin_mode);
	
	if ((pin >= 0 && pin <=16) && (pin_stat > DIO_NONE && pin_stat <= __DIO_LAST) && (pin_mode == EASYGPIO_INPUT || pin_mode == EASYGPIO_OUTPUT)) {
		p_cur_work->p_config = p_cur_config;
		p_cur_work->index = index;
		rv = easygpio_pinMode(pin, pin_stat, pin_mode);
		
		MB_DIO_DEBUG("DIO:INIT:2:%d\n", rv);

		if (pin_mode == EASYGPIO_INPUT) {
			MB_DIO_DEBUG("DIO:INIT:3:%d\n", rv);

			if (easygpio_attachInterrupt(pin, pin_stat, mb_dio_intr_handler, NULL)) {
				gpio_pin_intr_state_set(GPIO_ID_PIN(pin), pin_trig);
				p_cur_work->state_old = 0xff;
				p_cur_work->state = p_cur_config->init_state;
				MB_DIO_DEBUG("DIO:INIT:INPUT:%d\n", pin);
			}
		} else {
			p_cur_work->timer_run = 0;
			p_cur_work->state = p_cur_config->init_state;
			p_cur_work->state_old = !p_cur_config->init_state;
			easygpio_outputEnable(pin, p_cur_work->state);
		}
	}
	return rv;
}


LOCAL void ICACHE_FLASH_ATTR mb_dio_hw_init_all() {
	int i=0;
	for (i;i<MB_DIO_ITEMS;i++) {
		mb_dio_hw_init(i);
	}
}

void ICACHE_FLASH_ATTR mb_dio_handler(
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
	
	mb_dio_config_t *p_config = p_dio_config;
	int current_dio_id = -1;
	bool is_reinit = false;
	
	bool is_post = true; // 1=POST CFG, 2=POST command
	mb_dio_work_t *p_work = NULL;
	
	// post config for INIT
	if (method == POST && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);
		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Auto") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					p_config->autostart = jsonparse_get_value_as_int(&parser);
					MB_DIO_DEBUG("DIO:CFG:Auto:%d\n",p_config->autostart);
				} else if (jsonparse_strcmp_value(&parser, "Dio") == 0) {	// DioX; 0,1,2,3,4
					jsonparse_next(&parser);jsonparse_next(&parser);
					current_dio_id = jsonparse_get_value_as_int(&parser);
					MB_DIO_DEBUG("DIO:CFG:Dio:%d\n", current_dio_id);
				} else if (jsonparse_strcmp_value(&parser, "Type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].type = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Type:%d\n", current_dio_id, p_config->items[current_dio_id].type);
						is_reinit = true;
					}
				}
				else if (jsonparse_strcmp_value(&parser, "Gpio") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].gpio_pin = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Gpio:%d\n", current_dio_id, p_config->items[current_dio_id].gpio_pin);
						is_reinit = true;
					}
				} else if (jsonparse_strcmp_value(&parser, "Init") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].init_state = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Init:%d\n", current_dio_id, p_config->items[current_dio_id].init_state);
						is_reinit = true;
					}
				} else if (jsonparse_strcmp_value(&parser, "Inv") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].inverse = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Inv:%d\n", current_dio_id, p_config->items[current_dio_id].inverse);
						is_reinit = true;
					}
				} else if (jsonparse_strcmp_value(&parser, "Pls_on") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].pls_on = jsonparse_get_value_as_long(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Pls_on:%d\n", current_dio_id, p_config->items[current_dio_id].pls_on);
						is_reinit = true;
					}
				} else if (jsonparse_strcmp_value(&parser, "Pls_off") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						p_config->items[current_dio_id].pls_off = jsonparse_get_value_as_long(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Pls_off:%d\n", current_dio_id, p_config->items[current_dio_id].pls_off);
						is_reinit = true;
					}
				}
				else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					int tmpisStart = jsonparse_get_value_as_int(&parser);
					mb_dio_hw_init_all();
					MB_DIO_DEBUG("DIO:JSON:Started DIO!\n");
				}
				else {
					is_post = false;
					int cur_id=0;
					char tmp_str[10];
					do {
						os_sprintf(tmp_str, "Output%d", cur_id);
					} while ((jsonparse_strcmp_value(&parser, tmp_str) != 0) && ++cur_id<MB_DIO_ITEMS);
					if (cur_id>=0 && cur_id<MB_DIO_ITEMS && dio_work[cur_id].p_config != NULL) {
						jsonparse_next(&parser);jsonparse_next(&parser);
						p_work = &dio_work[cur_id];
						p_work->state_old = p_work->state;
						p_work->state = jsonparse_get_value_as_int(&parser);
						if (!p_work->state && p_work->timer_run) {
							os_timer_disarm(&p_work->timer);
							p_work->timer_run = false;
						}
						mb_dio_set_output(p_work);
						MB_DIO_DEBUG("DIO:CFG:DIO%d.Dout:%d\n", cur_id, p_work->state);
					}
				}

			}
		}
	}
	
	mb_dio_set_response(response, p_work, is_post);
}

/* Main Initialization file
 */
void ICACHE_FLASH_ATTR mb_dio_init() {
	bool isStartReading = false;
	p_dio_config = (mb_dio_config_t *)p_user_app_config_data->dio;		// set proper structure in app settings
		
	webserver_register_handler_callback(MB_DIO_URL, mb_dio_handler);
	device_register(NATIVE, 0, MB_DIO_URL, NULL, NULL);

	
	if (!user_app_config_is_config_valid())
	{
		isStartReading = (p_dio_config->autostart == 1);
		
		MB_DIO_DEBUG("DIO:Init with defaults!");
	}
	
	if (isStartReading) {
		mb_dio_hw_init_all();
		//mb_dht_timer_init(1);
	}
}

#endif
