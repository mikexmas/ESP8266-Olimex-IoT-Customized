#ifndef __MB_APP_CONFIG_H__
	#define __MB_APP_CONFIG_H__
	
	#include "c_types.h"
	#include "user_webserver.h"
	
	#define MB_APP_CONFIG_URL            "/appconfig"
	#define MB_APP_CONFIG__URL           "/appconfig/"
	
	#define MB_APP_CONFIG_START_SECTOR   0x105

	typedef struct {
		uint16 validandlength;	// validity: 6 bits; length: 10bites a 4 bytes aligment; 3*4 = 12
		uint16 crc;
	} user_app_config_head_t;
	
	typedef struct {
		uint32 adc[32];
		uint32 dht[32];
		uint32 ping[32];
		uint32 dio[32];
	} user_app_config_data_t;
	
	extern user_app_config_data_t *p_user_app_config_data;
	
	void user_app_config_init() ;
	
	uint8 user_app_config_is_config_valid();
	
	bool user_app_config_load(uint32 *p_buffer, uint32 buffer_size);
	
	bool user_app_config_store();

#endif
