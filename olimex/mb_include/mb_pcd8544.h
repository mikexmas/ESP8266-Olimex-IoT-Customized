#ifndef __MB_PCD8544_H__
	#define __MB_PCD8544_H__
	
	#include "mb_main.h"
			
	#if MB_PCD8544_ENABLE
		#define MB_PCD8544_DEBUG 1
		
		#define MB_PCD8544_DEVICE	"PCD8544"
		
		#define MB_PCD8544_URL     "/pcd8544"
		
		#define MB_PCD8544_RESET_PIN_DEFAULT	-1
		#define MB_PCD8544_SCE_PIN_DEFAULT		-1
		#define MB_PCD8544_DC_PIN_DEFAULT		12
		#define MB_PCD8544_SDIN_PIN_DEFAULT		13
		#define MB_PCD8544_SCLK_PIN_DEFAULT		14
		
		typedef struct {
			uint8 resetPin;
			uint8 scePin;
			uint8 dcPin;
			uint8 sdinPin;
			uint8 sclkPin;
		} mb_pcd8544_config_t;		// 
		
		void mb_pcd8544_init();
		
		void pcd8544_handler(
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
