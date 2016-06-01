#include "mb_main.h"

#include "osapi.h"

void ICACHE_FLASH_ATTR mb_main() {

	user_app_config_init();

#if MB_DHT_ENABLE
	mb_dht_init(false);
#endif

#if MB_AIN_ENABLE
	mb_ain_init(false);
#endif
	
#if MB_PING_ENABLE
	mb_ping_init(false);
#endif

#if MB_DIO_ENABLE
	mb_dio_init();
#endif

#if MB_PCD8544_ENABLE
	// just for test
	PCD8544_Settings *tmppcd;
	PCD8544_init(tmppcd);
#endif

}

#if MB_ACTIONS_ENABLE
/* Actions triggering from other; call make using setTimeout */
void ICACHE_FLASH_ATTR mb_action_post(mb_action_data_t *p_act_data) {
	uint8 action_type = MB_ACTIONTYPE_NONE;
	char data[WEBSERVER_MAX_VALUE];
	if (p_act_data != NULL) {
		action_type = p_act_data->action_type;
	}
#if MB_DIO_ENABLE
	if (action_type >= MB_ACTIONTYPE_DIO_0 && action_type <= MB_ACTIONTYPE_DIO_LAST) {
		uint8 dio_id = action_type - MB_ACTIONTYPE_DIO_0;
		os_sprintf(data, "{\"Output%d\": %d}", dio_id, p_act_data->value);
		if (action_type >= MB_ACTIONTYPE_DIO_0 && action_type <= MB_ACTIONTYPE_DIO_LAST) {
			mb_dio_handler(NULL, POST, MB_DIO_URL, data, os_strlen(data), os_strlen(data), NULL, 0);
		}
	}
#endif
}
#endif

