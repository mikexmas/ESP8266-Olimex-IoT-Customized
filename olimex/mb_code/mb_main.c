#include "mb_main.h"

void ICACHE_FLASH_ATTR mb_main() {

	user_app_config_init();

#ifdef DHT_ENABLE
	mb_dht_init(false);
#endif

#ifdef MB_ADC_ENABLE
	mb_adc_init(false);
#endif
	
#ifdef PING_ENABLE
	mb_ping_init(false);
#endif

#ifdef MB_DIO_ENABLE
	mb_dio_init(false);
#endif


}
