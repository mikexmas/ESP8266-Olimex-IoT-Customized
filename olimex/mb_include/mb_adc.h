#ifndef __MB_ADC_H__
	#define __MB_ADC_H__
	
	#include "mb_main.h"
	
	#if MB_ADC_ENABLE
	
		#define MB_ADC_URL      "/adc"
		#define MB_ADC_DEVICE	"ADC"
		
		#define MB_ADC_AUTOSTART_DEFAULT         0
		#define MB_ADC_REFRESH_DEFAULT           5000
		#define MB_ADC_EACH_DEFAULT              3
		#define MB_ADC_THRESHOLD_DEFAULT         1.0f
		#define MB_ADC_SCALE_K_DEFAULT           1.0f
		#define MB_ADC_SCALE_Y_DEFAULT           0.0f
		
		#define MB_ADC_DEBUG                     1
		
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
		} mb_adc_config_t;
		
		void mb_adc_init(uint8 isStartReading);
		
		void mb_adc_handler(
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