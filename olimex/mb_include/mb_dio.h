#ifndef __MB_DIO_H__	
	#define __MB_DIO_H__
	
	#include "mb_main.h"
	
	#if MB_DIO_ENABLE
	
		#define MB_DIO_DEBUG 1

		#define MB_DIO_DEVICE	"DIO"
		#define MB_DIO_URL      "/dio"

		#define MB_DIO_ITEMS		4		// default DIO items
		#define MB_DIO_FLT_TOUT		50		// normal TOUT filter time to stabilize input

		void mb_dio_init();
		
		typedef enum {
			DIO_NONE		= 0x00,

			DIO_IN_NOPULL	= 10,		// STD INPUT NO PULL-UP/DOWN, normal UP & DOWN triggering
			DIO_IN_PULLUP	= 11,		// STD INPUT PULL-UP, normal UP & DOWN triggering
			DIO_IN_PU_POS	= 12,		// PULL-UP, up transition trigger
			DIO_IN_NP_POS	= 13,		// NO-PULL, up transition trigger
			DIO_IN_PU_NEG	= 14,		// PULL-UP, down transition trigger
			DIO_IN_NP_NEG	= 15,		// NO-PULL, down transition trigger
			__DIO_IN_LAST   = DIO_IN_PULLUP,

			DIO_OUT			= 30,		// OUT NORMAL
			DIO_OUT_PULSE1	= 31,		// OUT 1 PULSE
			__DIO_OUT_LAST  = DIO_OUT,
			
			__DIO_LAST      = __DIO_OUT_LAST
			} mb_dio_type_t;
		
		typedef struct {
			uint8 gpio_pin;
			uint8 type;			// 0=none,...mb_dio_type_t
			uint8 init_state;	// out init state
			uint8 inverse;		// 1= inverse logic of electrical state
			
			uint32 pls_on;	// length of the pulse (mseconds)
			uint32 pls_off;	// length of the pulse OFF(mseconds)
			
			char name[12];
			
		} mb_dio_config_item_t;
		
		typedef struct {
			uint8 autostart;
			uint8 free1;
			uint8 free2;
			uint8 free3;
			mb_dio_config_item_t items[MB_DIO_ITEMS];
		} mb_dio_config_t;

		typedef struct {
			mb_dio_config_item_t *p_config;
			uint8 state;
			uint8 state_old;
			uint8 state_new;	// new state (used for filtering)
			os_timer_t timer;	// filter timer / 
			uint8 timer_run;
			uint8 index;		// index in array
			
		} mb_dio_work_t;
		
		void mb_dio_handler(
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