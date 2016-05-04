#ifndef __MB_AIN_H__
	#define __MB_AIN_H__
	
	#include "mb_main.h"
	
	#if MB_AIN_ENABLE
	
		#define MB_AIN_URL      "/ain"
		#define MB_AIN_DEVICE	"AIN"
		
		#define MB_AIN_AUTOSTART_DEFAULT         0
		#define MB_AIN_REFRESH_DEFAULT           5000
		#define MB_AIN_EACH_DEFAULT              3
		#define MB_AIN_THRESHOLD_DEFAULT         1.0f
		#define MB_AIN_SCALE_K_DEFAULT           1.0f
		#define MB_AIN_SCALE_Y_DEFAULT           0.0f
		#define MB_AIN_POST_TYPE_DEFAULT 		 MB_POSTTYPE_DEFAULT
		#define MB_AIN_ACTION_DEFAULT			 MB_ACTIONTYPE_NONE
		
		#define MB_AIN_DEBUG                     1
		
		typedef struct {
			uint8 autostart;
			uint8 each;
			uint8 decimals;
			uint8 post_type;
			uint32 refresh;
			float threshold;
			float scale_k;	// care about aligning 4 bytes
			float scale_y;	// care about aligning 4 bytes
			char name[12];	// 11 chars + null; name for POSTing data to eg. THINGSPEAK
			float low;		// IFTTT limits
			float hi;
			uint8 action;	// internal action
			uint8 free1;
			uint8 free2;
			uint8 free3;
		} mb_ain_config_t;	// 11 dwords
		
		void MB_AIN_init(uint8 isStartReading);
		
		void mb_ain_handler(
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