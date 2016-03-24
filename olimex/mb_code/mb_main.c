#include "mb_main.h"

#include "osapi.h"

void ICACHE_FLASH_ATTR mb_main() {

	user_app_config_init();

#if MB_DHT_ENABLE
	mb_dht_init(false);
#endif

#if MB_ADC_ENABLE
	mb_adc_init(false);
#endif
	
#if MB_PING_ENABLE
	mb_ping_init(false);
#endif

#if MB_DIO_ENABLE
	mb_dio_init();
#endif
}
