#ifndef __MB_DHT_H__	
	#define __MB_DHT_H__
	
	#include "mb_main.h"
	
	#if MB_DHT_ENABLE
	
		#define MB_DHT_DEBUG 1

		void mb_dht_init(bool isStartReading);
		
		typedef struct {
			uint8 autostart;
			uint8 gpio_pin;
			uint8 dht_type;		// 0=DHT11, 1=DTH22
			uint8 units;		// 0=C , 1=F

			// measurement sending parameters
			uint32 refresh;

			float threshold_t;
			float threshold_h;
			float offset_t;
			float offset_h;

			uint8 each;
			uint8 post_type;	// POST TYPE: Normal / ThingSpeak / IFTTT Maker Channel (Low/Hi limits Sending)
			uint8 action;	// ACTION TYPE: 0x01 = DO (Relay)
			uint8 free3;		// 4x 7 bytes = 28 bytes
			
			char name_t[12];	// 11 chars + null; name for POSTing data to eg. THINGSPEAK
			char name_h[12];	// 28+24 = 52 bytes ; 52/4 = 13 4xbytes
			
			float low_t;
			float hi_t;
			float low_h;
			float hi_h;
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

		#define MB_DHT_ERROR_COUNT				 16		// count till notify error
		#define MB_DHT_DECIMALS					 1		// number of decimals 
		
		#define MB_DHT_EVENT_NOTIFY_INIT		 0		// IFTTT event notify: initial state
		#define MB_DHT_EVENT_NOTIFY_HI		 	 1		// notified OUT-HI of bounds
		#define MB_DHT_EVENT_NOTIFY_LOW		 	 2		// notified OUT-LOW of bounds
		#define MB_DHT_EVENT_NOTIFY_IN		 	 3		// nofified IN bounds
		
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