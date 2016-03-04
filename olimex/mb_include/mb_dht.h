#ifndef __USER_DHT_H__	
	#define __USER_DHT_H__
	
	#include "mb_main.h"
	
	#if DHT_ENABLE
	
		#define DHT_DEBUG 1

		void mb_dht_init(bool isStartReading);
		
		typedef struct {
			uint8 autostart;
			uint8 gpio_pin;
			uint8 dht_type;		// 0=DHT11, 1=DTH22
			uint8 units;		// 0=C , 1=F
			
			uint32 refresh;
			float threshold_t;
			float threshold_h;
			float offset_t;
			float offset_h;

			uint8 each;
			uint8 free1;
			uint8 free2;
			uint8 free3;		// 4x 7 bytes = 28 bytes
			
			char name_t[12];	// 11 chars + null; name for POSTing data to eg. THINGSPEAK
			char name_h[12];	// 28+24 = 52 bytes ; 52/4 = 13 4xbytes
		} mb_dht_config_t;
		
		#define MB_DHT_AUTOSTART				 0		// no default autostart
		#define MB_DHT_TYPE_DEFAULT              1		// 0=DHT11/1=DHT22
		#define MB_DHT_GPIO_PIN_DEFAULT			 12		// default GPIO PIN
		#define MB_DHT_REFRESH_DEFAULT           10		// measurement interval
		#define MB_DHT_EACH_DEFAULT              3		// send each x measurement
		#define MB_DHT_T_THRESHOLD_DEFAULT       1		// treshold for T
		#define MB_DHT_H_THRESHOLD_DEFAULT       2		// treshold for H
		#define MB_DHT_T_OFFSET_DEFAULT       	 0 		// offset for T
		#define MB_DHT_H_OFFSET_DEFAULT          0		// treshold for H
		#define MB_DHT_UNITS_DEFAULT             0		// 0=C / 1=F

		#define MB_DHT_DEVICE	"DHT"
		
		#define MB_DHT_URL      "/dht"
		
		void dht_handler(
			struct espconn *pConnection, 
			request_method method,
			char *url,
			char *data,
			uint16 data_len,
			uint32 content_len, 
			char *response,
			uint16 response_len
		);

	#endif
#endif