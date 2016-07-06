#include "mb_main.h"

#if MB_DIO_ENABLE

#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "queue.h"
#include "gpio.h"
#include "mem.h"

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
LOCAL void mb_dio_intr_handler(void *arg);
LOCAL void mb_dio_set_response(char *response, mb_dio_work_t *p_work, uint8 req_type);
LOCAL void mb_dio_set_output(mb_dio_work_t *p_work);
LOCAL void mb_dio_send_state(mb_dio_work_t *p_work);

// GPIO interruot timer to determine stabile output; exe for both NORMAL & LONG
LOCAL uint8 ICACHE_FLASH_ATTR mb_dio_intr_timer_exe(mb_dio_work_t *p_work) {
	os_timer_disarm(&p_work->timer);
	uint8 state_cur = GPIO_INPUT_GET(p_work->p_config->gpio_pin);
	uint8 handled = 0;

	// Check new state => event; when ANY edge detection => allways send
	if (((p_work->p_config->type >= DIO_IN_NOPULL && p_work->p_config->type <= DIO_IN_PULLUP)
			|| (p_work->p_config->type >= DIO_IN_NOPULL_LONG && p_work->p_config->type <= DIO_IN_PULLUP_LONG))
			&& (p_work->state != p_work->state_old)) {
		p_work->state = p_work->state_new;
		p_work->state_old = !p_work->state;
		if (p_work->p_config->inverse) {
			p_work->state = !p_work->state;
		}
		p_work->state_old = !p_work->state;
		handled = true;
	}
	// positive edge detection
	else if (((p_work->p_config->type >= DIO_IN_PU_POS && p_work->p_config->type <= DIO_IN_NP_POS)
			|| (p_work->p_config->type >= DIO_IN_PU_POS_LONG && p_work->p_config->type <= DIO_IN_NP_POS_LONG))
			&& (state_cur == 1)) {
		p_work->state = (p_work->p_config->inverse ? 0 : 1);
		p_work->state_old = !p_work->state;
		handled = true;
	}
	// negative edge detection
	else if (((p_work->p_config->type >= DIO_IN_PU_NEG && p_work->p_config->type <= DIO_IN_NP_NEG)
			|| (p_work->p_config->type >= DIO_IN_PU_NEG_LONG && p_work->p_config->type <= DIO_IN_NP_NEG_LONG))
			&& (state_cur == 0)) {
		p_work->state = (p_work->p_config->inverse ? 1 : 0);
		p_work->state_old = !p_work->state;
		handled = true;
	}

	return handled;
}

// GPIO interruot timer to determine stabile output
/* Interrupt timer for NORMAL press */
LOCAL void ICACHE_FLASH_ATTR mb_dio_intr_timer(mb_dio_work_t *p_work) {
	uint8 handled = mb_dio_intr_timer_exe(p_work);
	if (handled) {
		MB_DIO_DEBUG("DIO:Intr:Timer:Index:%d,Gpio:%d,State:%d,Old:%d\n", p_work->index, p_work->p_config->gpio_pin, p_work->state, p_work->state_old);
		mb_dio_send_state(p_work);
	}
}

/* Interrupt timer for LONG press */
LOCAL void ICACHE_FLASH_ATTR mb_dio_intr_timer_long_press(mb_dio_work_t *p_work) {
	uint8 handled = mb_dio_intr_timer_exe(p_work);
	os_timer_disarm(&p_work->timer2);
	// if its still pressed
	if (handled && p_work->state == 0x01) {
		MB_DIO_DEBUG("DIO:Intr:LONG PRESS:Index:%d,Gpio:%d,State:%d,Old:%d\n", p_work->index, p_work->p_config->gpio_pin, p_work->state, p_work->state_old);
		//user_config_restore_defaults();
		//user_config_load();
	}
}

// GPIO interrupt handler
LOCAL void mb_dio_intr_handler(void *arg) {
	uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
  
	int i=0;
	for (i;i<MB_DIO_ITEMS;i++) {
		mb_dio_work_t *p_cur_work = &dio_work[i];
		if (p_cur_work->p_config != NULL && p_cur_work->p_config->type >=DIO_IN_NOPULL && p_cur_work->p_config->type <=__DIO_IN_LAST &&  p_cur_work->p_config->gpio_pin >= 0 && (gpio_status & BIT(p_cur_work->p_config->gpio_pin))) {
			GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status & BIT(p_cur_work->p_config->gpio_pin));

			p_cur_work->state_new = GPIO_INPUT_GET(p_cur_work->p_config->gpio_pin);		// remember state of input in interrupt

			// timer to determine stability of signal: it is settable
			os_timer_disarm(&p_cur_work->timer);
			os_timer_setfn(&p_cur_work->timer, (os_timer_func_t *)mb_dio_intr_timer, p_cur_work);
			os_timer_arm(&p_cur_work->timer, p_cur_work->flt_time, 0);
			
			// if long press => set timer
			if (p_cur_work->p_config->long_press == 0x01) {
				os_timer_disarm(&p_cur_work->timer2);
				os_timer_setfn(&p_cur_work->timer2, (os_timer_func_t *)mb_dio_intr_timer_long_press, p_cur_work);
				os_timer_arm(&p_cur_work->timer2, MB_DIO_LONG_PRESS, 0);
			}
		}
	}
}

/* Timeout when OUT pulse enabled */
LOCAL void ICACHE_FLASH_ATTR mb_dio_output_timer(mb_dio_work_t *p_work) {
	if (p_work != NULL && p_work->p_config != NULL) {
		os_timer_disarm(&p_work->timer);
		p_work->timer_run = 0;
		MB_DIO_DEBUG("DIO:OUTPUT_Timer:Id:%d,State:%d,StateOld:%d\n", p_work->index, p_work->state, p_work->state_old);
		p_work->state_old = p_work->state;
		p_work->state = !p_work->state;
		mb_dio_set_output(p_work);
		
		// send state update only when pulse ended => make separate timer not to call from this thread
		if (p_work->state == 0 && p_work->state_old == 1 && p_work->p_config->pls_on != 0 && p_work->p_config->pls_off == 0) {
				os_timer_disarm(&p_work->timer);
				os_timer_setfn(&p_work->timer, (os_timer_func_t *)mb_dio_send_state, p_work);
				os_timer_arm(&p_work->timer, 2, 0);
		}
	}	
}

/* Sets output of given GPIO */
LOCAL void ICACHE_FLASH_ATTR mb_dio_set_output(mb_dio_work_t *p_work) {
	if (p_work != NULL && p_work->p_config != NULL) {
		uint8 tmp_state = (p_work->p_config->inverse ? !p_work->state : p_work->state);
		GPIO_OUTPUT_SET(p_work->p_config->gpio_pin, tmp_state);
		MB_DIO_DEBUG("DIO:OutSet:Index:%d,Gpio:%d,State:%d,StateOld:%d,PhyPin:%d\n", p_work->index, p_work->p_config->gpio_pin, p_work->state, p_work->state_old, tmp_state);
		
		if (!p_work->state_old && p_work->state && p_work->p_config->pls_on != 0) {	// set timeout when pulse
			if (!p_work->timer_run) {
				os_timer_disarm(&p_work->timer);
				os_timer_setfn(&p_work->timer, (os_timer_func_t *)mb_dio_output_timer, p_work);
				os_timer_arm(&p_work->timer, p_work->p_config->pls_on, 0);
				p_work->timer_run = 1;
			}
		} else if (p_work->state_old && !p_work->state && p_work->p_config->pls_off != 0) {	// set timeout when repeating puls
			if (!p_work->timer_run) {
				os_timer_disarm(&p_work->timer);
				os_timer_setfn(&p_work->timer, (os_timer_func_t *)mb_dio_output_timer, p_work);
				os_timer_arm(&p_work->timer, p_work->p_config->pls_off, 0);
				p_work->timer_run = 1;
			}
		}
	}
	
	return;
}

// Helper to prepare and send rerspone
LOCAL void ICACHE_FLASH_ATTR mb_dio_send_state(mb_dio_work_t *p_work) {
	char response[WEBSERVER_MAX_RESPONSE_LEN];
	if (p_work->p_config->post_type == MB_POSTTYPE_THINGSPEAK || p_work->p_config->post_type == MB_POSTTYPE_IFTTT) {	// special messaging
		mb_dio_set_response(response, p_work, MB_REQTYPE_SPECIAL);
		webclient_post(user_config_events_ssl(), user_config_events_user(), user_config_events_password(), user_config_events_server(), user_config_events_ssl() ? WEBSERVER_SSL_PORT : WEBSERVER_PORT, user_config_events_path(), response);
	}
	mb_dio_set_response(response, p_work, MB_REQTYPE_NONE);
	user_event_raise(MB_DIO_URL, response);
	return;
}

// prepare response for event consumer
LOCAL void ICACHE_FLASH_ATTR mb_dio_set_response(char *response, mb_dio_work_t *p_work, uint8 req_type) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, MB_DIO_DEVICE, USER_CONFIG_USER_SIZE);
	
	MB_DIO_DEBUG("DIO web response preparing:req.type:%d\n",req_type);
	
	// POST request - status & config only
	if (req_type == MB_REQTYPE_POST) {
		int i=0;
		//char str_tmp[1024];
		char *str_tmp = (char *)os_malloc(1024);
		str_tmp[0] = 0x00;
		for (i;i<MB_DIO_ITEMS;i++) {
			mb_dio_config_item_t *p_cur_config = &p_dio_config->items[i];
			if (p_cur_config->gpio_pin < 0x20) {
				os_sprintf(data_str, ", \"Dio%d\":{\"Gpio\": %d, \"Type\":%d, \"Init\": %d, \"Inv\": %d, \"Pls_on\": %d, \"Pls_off\": %d, \"Name\":\"%s\", \"Post_type\":%d, \"Long_press\":%d,\"Conf\":%d}", i, p_cur_config->gpio_pin, p_cur_config->type, p_cur_config->init_state, p_cur_config->inverse, p_cur_config->pls_on, p_cur_config->pls_off, p_cur_config->name, p_cur_config->post_type, p_cur_config->long_press, dio_work->p_config > 0);
			}
			else {
				os_sprintf(data_str, ", \"Dio%d\":\"UNSET\"", i);
			}
			os_strcat(str_tmp, data_str);
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
		
		os_free(str_tmp);

	// event: do we want special format (thingspeak)
	} else if (req_type==MB_REQTYPE_SPECIAL && p_work != NULL && p_work->p_config != NULL && p_work->p_config->post_type == MB_POSTTYPE_THINGSPEAK) {		// states change only
		json_sprintf(
			response,
			"{\"api_key\":\"%s\", \"%s\":%d}",
			user_config_events_token(),
			(os_strlen(p_work->p_config->name) == 0 ? "field1" : p_work->p_config->name),
			p_work->state
		);

	// event: do we want special format (IFTTT): { "value1" : "", "value2" : "", "value3" : "" }
	} else if (req_type==MB_REQTYPE_SPECIAL && p_work != NULL && p_work->p_config != NULL && p_work->p_config->post_type == MB_POSTTYPE_IFTTT) {		// states change only
		char signal_name[20];
		signal_name[0] = 0x00;
		if (os_strlen(p_work->p_config->name) == 0) {
			if (p_work->p_config->type >= DIO_IN_NOPULL && p_work->p_config->type<=__DIO_IN_LAST)
				os_sprintf(signal_name, "DIO-IN_%d", p_work->p_config->gpio_pin);
			else
				os_sprintf(signal_name, "DIO-OUT_%d", p_work->p_config->gpio_pin);
		}
		else {
			os_strncpy(signal_name, p_work->p_config->name, MB_VARNAMEMAX);
		}
		json_sprintf(
			response,
			"{\"value1\":\"%s\",\"value2\":\"%d\"}",
			signal_name,
			p_work->state
		);

	// normal event measurement: get or nothing else happened
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
					} else if (p_cur_config->type >= DIO_OUT_NOPULL && p_cur_config->type <= __DIO_LAST) {
						os_sprintf(tmp_1, "%s\"Output%d\": %d", (str_tmp[0] == 0x00 ? "" : ","), i, p_cur_work->state);
					}
					os_strcat(str_tmp, tmp_1);
				}
			}
		} else if (p_work->p_config != NULL) {
			mb_dio_config_item_t *p_cur_config = p_work->p_config;
			if (p_cur_config->type >= DIO_IN_NOPULL && p_cur_config->type <= __DIO_IN_LAST) {
				os_sprintf(str_tmp, "\"Input%d\": %d", p_work->index, p_work->state);
			} else if (p_cur_config->type >= DIO_OUT_NOPULL && p_cur_config->type <= __DIO_OUT_LAST) {
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

/* HW init from config */
LOCAL bool ICACHE_FLASH_ATTR mb_dio_hw_init(int index) {
	bool rv = false;
	mb_dio_config_item_t *p_cur_config = &p_dio_config->items[index];
	mb_dio_work_t *p_cur_work = &dio_work[index];
	p_cur_work->p_config = NULL;
	
	int pin = p_cur_config->gpio_pin;
	EasyGPIO_PullStatus pin_stat = -1;
	EasyGPIO_PinMode pin_mode = 0;
	GPIO_INT_TYPE pin_trig = GPIO_PIN_INTR_ANYEDGE;		// input trigger
	switch (p_cur_config->type) {
	case DIO_IN_NOPULL:
	case DIO_IN_NOPULL_LONG:
		pin_stat = EASYGPIO_NOPULL;
		pin_mode = EASYGPIO_INPUT;
		break;
	case DIO_IN_PULLUP:
	case DIO_IN_PULLUP_LONG:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_INPUT;
		break;
	case DIO_IN_PU_POS:
	case DIO_IN_PU_POS_LONG:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_INPUT;
		pin_trig = GPIO_PIN_INTR_POSEDGE;
		break;
	case DIO_IN_NP_POS:
	case DIO_IN_NP_POS_LONG:
		pin_stat = EASYGPIO_NOPULL;
		pin_mode = EASYGPIO_INPUT;
		pin_trig = GPIO_PIN_INTR_POSEDGE;
		break;
	case DIO_IN_PU_NEG:
	case DIO_IN_PU_NEG_LONG:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_INPUT;
		pin_trig = GPIO_PIN_INTR_NEGEDGE;
		break;
	case DIO_IN_NP_NEG:
	case DIO_IN_NP_NEG_LONG:
		pin_stat = EASYGPIO_NOPULL;
		pin_mode = EASYGPIO_INPUT;
		pin_trig = GPIO_PIN_INTR_NEGEDGE;
		break;

	case DIO_OUT_NOPULL:
		pin_stat = EASYGPIO_NOPULL;
		pin_mode = EASYGPIO_OUTPUT;
		break;
	case DIO_OUT_PULLUP:
		pin_stat = EASYGPIO_PULLUP;
		pin_mode = EASYGPIO_OUTPUT;
		break;
	}
	
	if ((pin >= 0 && pin <=16) && (pin_stat > DIO_NONE && pin_stat <= __DIO_LAST) && (pin_mode == EASYGPIO_INPUT || pin_mode == EASYGPIO_OUTPUT)) {
		p_cur_work->p_config = p_cur_config;
		p_cur_work->index = index;
		rv = easygpio_pinMode(pin, pin_stat, pin_mode);
		
		if (rv) {
			if (pin_mode == EASYGPIO_INPUT) {
				if (easygpio_attachInterrupt(pin, pin_stat, mb_dio_intr_handler, NULL)) {
					gpio_pin_intr_state_set(GPIO_ID_PIN(pin), pin_trig);
					p_cur_work->state = (p_cur_work->p_config->inverse ? 1 : 0);
					p_cur_work->state_old = !p_cur_work->state;
					if (p_cur_config->type >= DIO_IN_NOPULL_LONG && p_cur_config->type <= DIO_IN_NP_NEG_LONG)	// long filter pulse ?
						p_cur_work->flt_time = MB_DIO_FLT_LONG;
					else
						p_cur_work->flt_time = MB_DIO_FLT_TOUT;
					MB_DIO_DEBUG("DIO:INIT:Index:%d,Gpio:%d,Trig:%d,State:%d,FltTime:%d\n", index, pin, pin_trig, p_cur_work->state, p_cur_work->flt_time);
				}
			} else {
				p_cur_work->timer_run = 0;
				p_cur_work->state = p_cur_config->init_state;
				p_cur_work->state_old = !p_cur_config->init_state;
				easygpio_outputEnable(pin, p_cur_work->state);
				if (!p_cur_work->state && p_cur_work->timer_run) {
					os_timer_disarm(&p_cur_work->timer);
					p_cur_work->timer_run = false;
				}
				mb_dio_set_output(p_cur_work);
			}
		} else {
			MB_DIO_DEBUG("DIO:INIT_FAILED:Index:%d,Gpio:%d\n", index, pin);
		}
		
	}
	return rv;
}

/* HW init loop */
LOCAL void ICACHE_FLASH_ATTR mb_dio_hw_init_all() {
	int i=0;
	for (i;i<MB_DIO_ITEMS;i++) {
		mb_dio_hw_init(i);
	}
}

/* REST/JSON handler */
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
	
	mb_dio_config_t *p_config = p_dio_config;
	int current_dio_id = -1;
	
	bool is_post = (method == POST);
	bool is_post_cfg = false;	// do we actually received cfg (to make config)
	uint8 tmp_ret = 0xFF;
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
						is_post_cfg = true;
						p_config->items[current_dio_id].type = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Type:%d\n", current_dio_id, p_config->items[current_dio_id].type);
					}
				}
				else if (jsonparse_strcmp_value(&parser, "Gpio") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].gpio_pin = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Gpio:%d\n", current_dio_id, p_config->items[current_dio_id].gpio_pin);
					}
				} else if (jsonparse_strcmp_value(&parser, "Init") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].init_state = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Init:%d\n", current_dio_id, p_config->items[current_dio_id].init_state);
					}
				} else if (jsonparse_strcmp_value(&parser, "Inv") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].inverse = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Inv:%d\n", current_dio_id, p_config->items[current_dio_id].inverse);
					}
				} else if (jsonparse_strcmp_value(&parser, "Pls_on") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].pls_on = jsonparse_get_value_as_long(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Pls_on:%d\n", current_dio_id, p_config->items[current_dio_id].pls_on);
					}
				} else if (jsonparse_strcmp_value(&parser, "Pls_off") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].pls_off = jsonparse_get_value_as_long(&parser);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Pls_off:%d\n", current_dio_id, p_config->items[current_dio_id].pls_off);
					}
				} else if (jsonparse_strcmp_value(&parser, "Name") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						jsonparse_copy_value(&parser, p_config->items[current_dio_id].name, MB_VARNAMEMAX);
						MB_DIO_DEBUG("DIO:CFG:Dio%d.Name:%s\n", current_dio_id, p_config->items[current_dio_id].name);
					}
				} else if (jsonparse_strcmp_value(&parser, "Post_type") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].post_type = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Post_type:%d\n", p_config->items[current_dio_id].post_type);
					}
				} else if (jsonparse_strcmp_value(&parser, "Long_press") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					if (current_dio_id>=0 && current_dio_id<MB_DIO_ITEMS) {
						is_post_cfg = true;
						p_config->items[current_dio_id].long_press = jsonparse_get_value_as_int(&parser);
						MB_DIO_DEBUG("DIO:CFG:Long_press:%d\n", p_config->items[current_dio_id].long_press);
					}
				}
				else if (jsonparse_strcmp_value(&parser, "Start") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					int tmpisStart = jsonparse_get_value_as_int(&parser);
					if (tmpisStart) {
						is_post_cfg = true;
						MB_DIO_DEBUG("DIO:CFG:Started DIO!\n");
					}
				} else if (tmp_ret = user_app_config_handler_part(&parser) != 0xFF){	// check for common app commands
					MB_DIO_DEBUG("DIO:CFG:APPCONFIG:%d\n", tmp_ret);
				}
				else {
					// SET OUTPUT command (maybe)
					int cur_id=0;
					char tmp_str[10];
					do {
						os_sprintf(tmp_str, "Output%d", cur_id);
					} while ((jsonparse_strcmp_value(&parser, tmp_str) != 0) && ++cur_id<MB_DIO_ITEMS);
					MB_DIO_DEBUG("DIO:CMD1:DIO%d\n", cur_id);
					if (cur_id>=0 && cur_id<MB_DIO_ITEMS && dio_work[cur_id].p_config != NULL) {
						is_post = false;
						jsonparse_next(&parser);jsonparse_next(&parser);
						p_work = &dio_work[cur_id];
						p_work->state_old = p_work->state;
						p_work->state = jsonparse_get_value_as_int(&parser);
						if (!p_work->state && p_work->timer_run) {
							os_timer_disarm(&p_work->timer);
							p_work->timer_run = false;
						}
						mb_dio_set_output(p_work);
						MB_DIO_DEBUG("DIO:CMD:DIO%d.Dout:%d\n", cur_id, p_work->state);
					}
				}
			}
		}

		// make config only when cfg command received
		if (is_post && is_post_cfg)
			mb_dio_hw_init_all();
	}
	
	// its possible to call this fnc not from webserver => then we should make an event
	if (pConnection == NULL && response == NULL) {
		mb_dio_send_state(p_work);
	}
	else {
		mb_dio_set_response(response, p_work, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
	}
}

/* Main Initialization file
 */
void ICACHE_FLASH_ATTR mb_dio_init() {
	bool isStartReading = false;
	p_dio_config = (mb_dio_config_t *)p_user_app_config_data->dio;		// set proper structure in app settings
		
	webserver_register_handler_callback(MB_DIO_URL, mb_dio_handler);
	device_register(NATIVE, 0, MB_DIO_DEVICE, MB_DIO_URL, NULL, NULL);

	if (!user_app_config_is_config_valid())
	{
		p_dio_config->autostart = false;
		int i=0;
		for (i;i<MB_DIO_ITEMS;i++) {
			p_dio_config->items[i].gpio_pin = 0xFF;	// make invalid for sure
		}

		MB_DIO_DEBUG("DIO:Init with defaults, no defaults!");
	}
	
	if (!isStartReading)
		isStartReading = (p_dio_config->autostart == 1);
	
	if (isStartReading) {
		mb_dio_hw_init_all();
	}
}

#endif
