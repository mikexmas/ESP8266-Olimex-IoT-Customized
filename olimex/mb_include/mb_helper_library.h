#ifndef __USER_HELPER_LIBRARY_H__
	#define __USER_HELPER_LIBRARY_H__
	
	#include "user_config.h"
	
	#define MB_LIMITS_NOTIFY_INIT		 0		// IFTTT event notify: initial state
	#define MB_LIMITS_NOTIFY_HI		 	 1		// notified OUT-HI of bounds
	#define MB_LIMITS_NOTIFY_LOW		 2		// notified OUT-LOW of bounds
	#define MB_LIMITS_NOTIFY_IN		 	 3		// nofified IN bounds
	
	extern const char DEVICE_STATUS_OK[];			// OK and running
	extern const char DEVICE_STATUS_FAULT[];		// FAULT (sensor FAULT)
	extern const char DEVICE_STATUS_STOP[];		// OK but no reading (stopped by user or not started)
	extern const char DEVICE_STATUS_ERROR[];		// ERROR, eg. np valid configuration
	
	extern const char MB_LIMITS_NONE[];
	extern const char MB_LIMITS_HI[];
	extern const char MB_LIMITS_LOW[];
	extern const char MB_LIMITS_IN[];
	
	float uhl_convert_c_to_f(float val_c);

	float uhl_convert_f_to_c(float val_f);
	
	float uhl_fabs(float val);
	
	/* Convert float number to string */
	char *uhl_flt2str(char* str, float val, int decimals);

	float uhl_str2flt(char* inputstr);
	
	float uhl_jsonparse_get_value_as_float(struct jsonparse_state *parser);
	
	void uhl_hexdump(uint8* p_data, int data_len, uint32 real_addr);
	
	void mb_make_full_device_name(char *p_dest, char *p_str, int maxlen);
	
	int mb_jsonparse_sint_str(struct jsonparse_state *parser);
	
	uint8 uhl_which_event(float val, float hi, float low, float thr, char **p_str);

#endif
