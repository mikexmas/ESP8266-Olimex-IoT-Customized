#ifndef __USER_HELPER_LIBRARY_H__
	#define __USER_HELPER_LIBRARY_H__
	
	#include "user_config.h"
	
	#define DEVICE_STATUS_OK		"OK"		// OK and running
	#define DEVICE_STATUS_FAULT		"FAULT"		// FAULT (sensor FAULT)
	#define DEVICE_STATUS_STOP		"STOP"		// OK but no reading (stopped by user or not started)
	#define DEVICE_STATUS_ERROR		"ERROR"		// ERROR, eg. np valid configuration

	
	float uhl_convert_c_to_f(float val_c);

	float uhl_convert_f_to_c(float val_f);
	
	bool uhl_fabs(float val);
	
	/* Convert float number to string */
	char *uhl_flt2str(char* str, float val, int decimals);

	float uhl_str2flt(char* inputstr);
	
	float uhl_jsonparse_get_value_as_float(struct jsonparse_state *parser);
	
	void uhl_hexdump(uint8* p_data, int data_len, uint32 real_addr);
	
	void mb_make_full_device_name(char *p_dest, char *p_str, int maxlen);
	
	int mb_jsonparse_sint_str(struct jsonparse_state *parser);

#endif
