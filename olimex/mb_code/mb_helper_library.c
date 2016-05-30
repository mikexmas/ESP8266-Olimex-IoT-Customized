#include "os_type.h"
#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "mem.h"
#include "user_misc.h"
#include "user_json.h"

#include "mb_helper_library.h"

// OUT, HI, LOW, IN
const char MB_LIMITS_NONE[] = "";
const char MB_LIMITS_HI[] = "HI";
const char MB_LIMITS_LOW[] = "LOW";
const char MB_LIMITS_IN[] = "IN";

const char DEVICE_STATUS_OK[] = "OK";			// OK and running
const char DEVICE_STATUS_FAULT[] = "FALUT";		// FAULT (sensor FAULT)
const char DEVICE_STATUS_STOP[] = "STOP";		// OK but no reading (stopped by user or not started)
const char DEVICE_STATUS_ERROR[] = "ERROR";		// ERROR, eg. np valid configuration

float ICACHE_FLASH_ATTR uhl_convert_c_to_f(float val_c) {
	return val_c * 1.8f + 32.0f;
}

float ICACHE_FLASH_ATTR uhl_convert_f_to_c(float val_f) {
  return (val_f - 32.0f) * 0.55555f;
}

float ICACHE_FLASH_ATTR uhl_fabs(float val) {
	return (val >= 0.0f ? val : val*(-1.0f));
}

char* ICACHE_FLASH_ATTR uhl_flt2str(char* str, float val, int decimals)
{
   int int_part = (int)val;
    float float_part = (float)((float)val - (float)int_part);
	if (float_part < 0.0f)
		float_part *= -1.0f;
    char str_dec[20];
	str_dec[0] = 0x00;
 
	str[0] = 0x00;
	if (val < 0.0f) {
		os_sprintf(str, "-%d", abs(int_part));	// convert integer part to string -
	}
	else {
		os_sprintf(str, "%d", abs(int_part));	// convert integer part to string +
	}

    // check for display option after point
    if (decimals > 0)
    {
        os_strcat(str, ".");  // add dot
		
		int cnt_dec = decimals;
		float double_part = float_part;
		//debug("f2str:decimals*10000:%d, double:%d\n", (long)(float_part*100000.0), (long)(double_part*100000.0));
		while (cnt_dec--) {
			double_part = double_part * 10.0f;
			int int_of_double_part = (int)double_part;
			//debug("f2str:cnt:%d,int:%d\n", cnt_dec, int_of_double_part);
			if (cnt_dec == 0) {		// round last digit
				float remaining = double_part - int_of_double_part;
				//debug("f2str:rem0*1000000:%d", (long)(remaining*1000000.0));
				if (int_of_double_part<9 && int_of_double_part>0) {
					if (remaining >= 0.5f)
						int_of_double_part++;
					else if (remaining <= -0.5f)
						int_of_double_part--;
				}
			} else if (cnt_dec == 1) {	// care about precision at last digit
				float remaining = double_part - int_of_double_part;
				if ((remaining > 0.999f) && (int_of_double_part <9))
					int_of_double_part++;
				//debug("f2str:rem1*1000000:%d", (long)(remaining*1000000.0));
			}
			//debug("f2str:int2:%d\n", int_of_double_part);
			char tmp_dec[10]; 
			os_sprintf(tmp_dec, "%01d", abs(int_of_double_part));
			os_strcat(str_dec, tmp_dec);
			//debug("f2str:tmpstr:%s\n", str_dec);
			double_part = double_part - (float)int_of_double_part;
		}
 
		int i;
		for (i = os_strlen(str_dec); i<decimals;i++)
		{
			os_strcat(str_dec, "0");
		}
		os_strcat(str, str_dec);
    }

	return str;
}

float ICACHE_FLASH_ATTR uhl_str2flt(char* inputstr)
{
    float result= 0.0f;
    int len = os_strlen(inputstr);
    int dotpos=0;
    int n;
	bool decimals = false;
	bool negative = false;

	char tmpStr[15];

	/*If the number was signed,then we set n to 1,so that we start with inputstr[1],and at the end if the number was negative we will multiply by -1.*/
    for (n=0; n < len; n++) {        //n is already set to the position of the fisrt number.
		if (inputstr[n] == ' ' || inputstr[n] == '+') {	// skip leading spaces
		} else if (inputstr[n] == '-') {
			negative = true;
		} else if (inputstr[n] == '.')
			decimals = true;
        else {
            result = result * 10.0f + (inputstr[n]-'0');
			if (decimals)
				dotpos ++;
		}
		uhl_flt2str(tmpStr, result, 3);
	}
	
    while (dotpos--) {
        result /= 10.0f;
		uhl_flt2str(tmpStr, result, 3);
	}
    if (negative)  			//If inputstr[] is "negative",
        result*=(-1);      	//multiply the number by -1,making it also negative.

	return result;
}

/******************************************************************************
 * FunctionName : jsonparse_object_str
 * Description  : 
 * Parameters   : 
*******************************************************************************/
float ICACHE_FLASH_ATTR uhl_jsonparse_get_value_as_float(struct jsonparse_state *parser) {
	char dst[25];
	dst[0] = 0x00;
	int dst_len = 24;
	float flt_val = 0.0f;
	bool is_negative = false;
	int pos_len = jsonparse_get_len(parser);
	
	//debug("JSON1:type:%d, parserPOS:%d, pos.len:%d\n", parser->vtype, parser->pos, jsonparse_get_len(parser));
	
	if (parser->vtype == JSON_TYPE_ERROR) {
		jsonparse_next(parser);
		is_negative = true;
		pos_len = jsonparse_get_len(parser);
	}

	if (parser->vtype == JSON_TYPE_NUMBER || parser->vtype == JSON_TYPE_PAIR) {
		if (pos_len < dst_len) {
			os_memcpy(dst, parser->json + parser->pos - pos_len, pos_len);
			dst[pos_len] = '\0';
			flt_val = uhl_str2flt(dst);
		}
	}
	
	if (is_negative)
		return flt_val * -1.0f;
	else
		return flt_val;
}

void ICACHE_FLASH_ATTR uhl_hexdump(uint8* p_data, int data_len, uint32 real_addr) {
	char line[80];
	line[0] = 0x0;
	char linetmp[10];
	uint8* p_cur;
	// 0x00000000|01 02 03 04 05 06 07 08 09 10 01 02 03 04 05 06
	debug("Mem hex dump: %X %d\n", real_addr, data_len);
	int i=0;
	os_sprintf(line, "%X|", real_addr);
	
	for (p_cur=p_data; p_cur < p_data + data_len; p_cur++) {
		i++;
		if (*p_cur <= 0x0F)
			os_sprintf(linetmp, " 0%X", *p_cur);
		else
			os_sprintf(linetmp, " %X", *p_cur);
		os_strcat(line, linetmp);
		real_addr++;
		if (i % 16 == 0) {
			debug("%s\n", line);
			os_delay_us(5000);
			line[0] = 0x0;
			os_sprintf(line, "%X|", real_addr);
		}
	}
	if (i % 16 != 0) {	// last line which may not be 16 bytes long
		debug("%X|%s\n", real_addr, line);
	}
	debug("Debug End\n");
}

void ICACHE_FLASH_ATTR mb_make_full_device_name(char *p_dest, char *p_str, int maxlen) {
	int len2 = os_strlen(p_str);
	
	char *p_hostname = wifi_station_get_hostname();	
	os_strncpy(p_dest, p_hostname, maxlen);
	int len1 = os_strlen(p_dest);
	
	int len2max = maxlen - (len1 + len2) - 1;
	
	if (len2max > 1) {
		os_strcat(p_dest, ".");
		os_strcat(p_dest, p_str);
	}
	
	int len3 = os_strlen(p_dest);
}

/******************************************************************************
 * FunctionName : jsonparse_get_value_as_float
 * Description  : 
 * Parameters   : 
*******************************************************************************/
int ICACHE_FLASH_ATTR jsonparse_get_value_as_float(struct jsonparse_state *parser) {
	if (parser->vtype == JSON_TYPE_ERROR) {
		jsonparse_next(parser);
		return -jsonparse_get_value_as_int(parser);
	}
	return jsonparse_get_value_as_int(parser);
}

/* Evaluate if and which event triggered IFTTT: OUT, HI, LOW, IN */
// ret: 0 nothing, 0x01=THI, 0x02= TLOW, IN=0x03, H=0x10....
uint8 ICACHE_FLASH_ATTR uhl_which_event(float val, float hi, float low, float thr, char **p_str) {
	uint8 ret = 0x00;

	*p_str = (char*)MB_LIMITS_NONE;	
	if (uhl_fabs(low - hi) > 1.0f) {
		if (val > hi) {
			ret = MB_LIMITS_NOTIFY_HI;
			*p_str = (char*)MB_LIMITS_HI;
		} else if (val < low) {
			ret = MB_LIMITS_NOTIFY_LOW;
			*p_str = (char*)MB_LIMITS_LOW;
		} else if ((val > low + thr) && (val < hi -  thr)) {
			ret = MB_LIMITS_NOTIFY_IN;
			*p_str = (char*)MB_LIMITS_IN;
		}
	}
	return ret;
}
