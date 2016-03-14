#include "mb_main.h"

#include "osapi.h"

void ICACHE_FLASH_ATTR mb_main() {

	user_app_config_init();

#ifdef MB_DHT_ENABLE
	mb_dht_init(false);
#endif

#ifdef MB_ADC_ENABLE
	mb_adc_init(false);
#endif
	
#ifdef MB_PING_ENABLE
	mb_ping_init(false);
#endif

#ifdef MB_DIO_ENABLE
	mb_dio_init();
#endif
}

LOCAL uint8 mb_events_post_spec_format = 0xff; 		// IOT server: 1=special format needed,0xff unset

// Called from EVENTS before iot posting data (user_event), we allow other types of data eg. thingspeak etc; there is specific format and no other events send to theese services
char ICACHE_FLASH_ATTR *mb_events_check_posttype(char *p_data, bool isIotEvent) {
	char *ret_val = p_data;
	
	// if not checked IOT server, check it
	if (mb_events_post_spec_format == 0xFF) {
		mb_events_post_spec_format = 0x00;
		char *p_iot_server = user_config_events_server();
		if (p_iot_server != NULL) {
			char *p_search = (char *)(os_strstr(p_iot_server, ".thingspeak."));
			if (p_search > p_iot_server && p_search < p_iot_server + 128)
				mb_events_post_spec_format = true;
			p_search = (char *)(os_strstr(p_iot_server, ".ifttt."));
			if (p_search > p_iot_server && p_search < p_iot_server + 128)
				mb_events_post_spec_format = true;
		}
	}
	
	if (p_data[0] == MB_POSTTYPE_HEAD_CHAR) {
		if (os_strncmp(p_data, MB_POSTTYPE_THINGSPEAK_STR, MB_POSTTYPE_HEAD_LEN) == 0) {
			ret_val = &p_data[MB_POSTTYPE_HEAD_LEN];
		}
		else if (os_strncmp(p_data, MB_POSTTYPE_IFTTT_STR, MB_POSTTYPE_HEAD_LEN) == 0) {
			ret_val = &p_data[MB_POSTTYPE_HEAD_LEN];
		}
		else {
			if (isIotEvent)
				ret_val = (char*)NULL;	// not send to IoT server
			else
				ret_val = &p_data[MB_POSTTYPE_HEAD_LEN];
		}
	} else if (mb_events_post_spec_format == 1 && isIotEvent) {
		ret_val = (char*)NULL;	// not send to IoT server xyz event data
	}
	
	//os_printf("MBMAIN:CHKPOST:data:%d,ret:%d\n", p_data, ret_val);

	return ret_val;
}
