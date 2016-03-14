#ifndef __MB_PING_H__
	#define __MB_PING_H__
	
	#include "mb_main.h"
			
	#if MB_PING_ENABLE
		#define MB_PING_DEBUG 1
		
		void mb_ping_init(bool isStartReading);
		
		typedef struct {
			uint8 autostart;
			uint8 trigger_pin;
			uint8 echo_pin;
			uint8 units;		// 0=mm, 1=inch, 2=us
			uint32 refresh;
			uint8 each;
			uint8 post_type;
			uint8 free2;
			uint8 free3;
			float max_distance;	// in selected units
			float threshold;
			float offset;
			char name[12];	// 11 chars + null; name for POSTing data to eg. THINGSPEAK
			float low;
			float hi;
		} mb_ping_config_t;
		
		#define MB_PING_AUTOSTART				 0		// no default autostart
		#define MB_PING_TRIGGER_PIN_DEFAULT		 12		// default trigger PIN
		#define MB_PING_ECHO_PIN_DEFAULT		 12		// default echo PIN
		#define MB_PING_UNITS_DEFAULT            0		// 0=m / 1=inches
		#define MB_PING_REFRESH_DEFAULT          10		// measurement interval
		#define MB_PING_EACH_DEFAULT             3		// send each x measurement
		#define MB_PING_MAXDISTANCE_DEFAULT      4000.0f	// max.distance (about 4 meters)
		#define MB_PING_THRESHOLD_DEFAULT        1.0f	// treshold 
		#define MB_PING_OFFSET_DEFAULT       	 0.0f	// offset
		
		#define MB_PING_DEVICE	"PING"
		
		#define MB_PING_URL     "/ping"
		
		void ping_handler(
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