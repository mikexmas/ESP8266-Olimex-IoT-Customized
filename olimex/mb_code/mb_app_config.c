#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "os_type.h"
#include "spi_flash.h"

#include "user_json.h"
#include "user_misc.h"
#include "user_flash.h"
#include "user_config.h"
#include "user_events.h"

#include "mb_main.h"
#include "mb_app_config.h"
#include "mb_helper_library.h"

user_app_config_data_t user_app_config_data;
user_app_config_data_t *p_user_app_config_data = &user_app_config_data;
bool user_app_config_data_valid = false;


LOCAL void ICACHE_FLASH_ATTR mb_appconfig_set_response(char *response, bool is_fault, uint8 req_type) {
	char data_str[WEBSERVER_MAX_RESPONSE_LEN];
	char full_device_name[USER_CONFIG_USER_SIZE];
	
	mb_make_full_device_name(full_device_name, "APPCFG", USER_CONFIG_USER_SIZE);
	
	debug("APPCFG:Resp.prep:%d;isFault:%d\n",req_type, is_fault);
	
	// Sensor fault
	if (is_fault) {
		json_error(response, full_device_name, DEVICE_STATUS_ERROR, NULL);
	}
	
	// POST request - status & config
	else if (req_type==MB_REQTYPE_POST) {
		json_status(response, full_device_name, DEVICE_STATUS_OK, NULL);

	// normal GET
	} else {
		json_status(response, full_device_name, DEVICE_STATUS_OK, NULL);
	}
}

void ICACHE_FLASH_ATTR user_app_config_handler (
	struct espconn *pConnection, 
	request_method method, 
	char *url, 
	char *data, 
	uint16 data_len, 
	uint32 content_len, 
	char *response,
	uint16 response_len
) {	
	struct jsonparse_state parser;
	int type;
	char tmp_str[20];
	bool ret = true;	// 1=OK, 0=failed operation (ERROR)
	
	bool is_post = (method == POST);	// it means POST config data
	
	if (os_strstr(url, "&hexdump")>0) {
		debug("APPCFG: Hexdump\n");
		uint8 buf[64];
		spi_flash_read(0x105000, (uint32*)&buf, 64);
		//uhl_hexdump(buf, 64, 0x105000);
	}
	
	if (is_post && data != NULL && data_len != 0) {
		jsonparse_setup(&parser, data, data_len);

		while ((type = jsonparse_next(&parser)) != 0) {
			if (type == JSON_TYPE_PAIR_NAME) {
				if (jsonparse_strcmp_value(&parser, "Save") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_get_value_as_int(&parser);
					ret = user_app_config_store();
					debug("APPCFG:SAVE:%d\n", ret);
				} else if (jsonparse_strcmp_value(&parser, "Erase") == 0) {
					jsonparse_next(&parser);jsonparse_next(&parser);
					jsonparse_get_value_as_int(&parser);
					if (spi_flash_erase_sector(MB_APP_CONFIG_START_SECTOR) == SPI_FLASH_RESULT_OK) {
						ret = 1;
					}
					else {
						ret = 0;
					}
					debug("APPCFG:ERASE:%d\n",ret);
				}
			}
		}
	}
	
	// mb_appconfig_set_response(response, !ret, is_post ? MB_REQTYPE_POST : MB_REQTYPE_GET);
}


uint8 ICACHE_FLASH_ATTR user_app_config_is_config_valid() {
	return user_app_config_data_valid;
}

void ICACHE_FLASH_ATTR user_app_config_init() {
	user_app_config_data_valid = false;
	
	flash_region_register("app-config", MB_APP_CONFIG_START_SECTOR, 0x001);
	webserver_register_handler_callback(MB_APP_CONFIG_URL, user_app_config_handler);

	
	// load
	user_app_config_data_t load_app_config;
	if (user_app_config_load((uint32 *) &load_app_config, sizeof(user_app_config_data_t))) {
		os_memcpy((uint8 *)&user_app_config_data, (uint8 *)&load_app_config, sizeof(user_app_config_data_t));
		user_app_config_data_valid = true;
	}
}

bool ICACHE_FLASH_ATTR user_app_config_load(uint32 *p_buffer, uint32 buffer_size) {
	user_app_config_head_t config_head;
	SpiFlashOpResult result;
	
	uint8 flashValid = false;
	uint32 flashSize = 0x0000;

	debug("APPCONFIG: Loading...\n");
	
	// load control head
	result = spi_flash_read(MB_APP_CONFIG_START_SECTOR * SPI_FLASH_SEC_SIZE, (uint32*)&config_head, sizeof(user_app_config_head_t));

	if (result != SPI_FLASH_RESULT_OK) {
		debug("APPCONFIG: Load control head failed\n\n");
		return false;
	}
	
	flashValid = (config_head.validandlength >> 10) & 0x3F ;		// higher 6 bits shifted to lower 6 bits
	if (flashValid != 0x00) {
		debug("APPCONFIG: Control head validity failed\n\n");
		return false;
	}
	
	flashSize = (config_head.validandlength & 0x3FF) * 4;
	
	if (flashSize != sizeof(user_app_config_data_t))
		return false;
		
	// Load data from flash
	result = spi_flash_read(MB_APP_CONFIG_START_SECTOR * SPI_FLASH_SEC_SIZE + sizeof(user_app_config_head_t), p_buffer, flashSize);
	
	if (result != SPI_FLASH_RESULT_OK) {
		debug("APPCONFIG: Load data failed\n\n");
		return false;
	}
	
	// Calculate CRC
	uint16 crc16_check = crc16((uint8 *)p_buffer, flashSize);
	
	if (crc16_check == config_head.crc) {
		debug("APPCONFIG: Load success\n\n");
	} else {
		debug("CONFIG: Data not valid or CRC error\n\n");
		//user_appconfig_restore_defaults();
		return false;
	}
		
	return true;
}

// stores app config
LOCAL bool ICACHE_FLASH_ATTR user_app_config_store_l(uint32 *p_buffer, uint32 buffer_size) {
	LOCAL const char NOT_ALIGNED[] = "Size of user_config must be aligned to 4 bytes";
	LOCAL const char ERASE_ERR[]   = "Can not erase user_config sector";
	LOCAL const char WRITE_ERR[]   = "Can not write user_config sector";
	
	user_app_config_head_t config_head;

	debug("APPCONFIG: Storing...\n");
	
	if (buffer_size % 4 != 0) {
		debug("APPCONFIG: %s\n", NOT_ALIGNED);
		return false;
	}
	
	if (spi_flash_erase_sector(MB_APP_CONFIG_START_SECTOR) != SPI_FLASH_RESULT_OK) {
		debug("APPCONFIG: %s\n", ERASE_ERR);
		return false;
	}
	
	// Write DATA
	SpiFlashOpResult result;
	result = spi_flash_write(MB_APP_CONFIG_START_SECTOR * SPI_FLASH_SEC_SIZE + sizeof(user_app_config_head_t), p_buffer, buffer_size);
	
	if (result != SPI_FLASH_RESULT_OK) {
		debug("APPCONFIG: %s\n", WRITE_ERR);
		return false;
	}

	// Write control head: 6 bits validity & 10 bits length /4
	config_head.validandlength = (0x0000 << 10) | (buffer_size /4) ;
	config_head.crc = crc16((uint8*)p_buffer, buffer_size);
	
	debug("APPCONFIG: %x\n", config_head.validandlength);
	
	result = spi_flash_write(MB_APP_CONFIG_START_SECTOR * SPI_FLASH_SEC_SIZE, (uint32*)&config_head, sizeof(config_head));
	
	if (result != SPI_FLASH_RESULT_OK) {
		debug("APPCONFIG: %s\n", WRITE_ERR);
		return false;
	} else {
		debug("APPCONFIG: Stored\n\n");
	}
	
	user_app_config_data_t loadtest_app_config;
	bool testLoadValid = user_app_config_load((uint32 *) &loadtest_app_config, sizeof(user_app_config_data_t));
	if (testLoadValid) {
		os_memcpy((uint8 *)&user_app_config_data, (uint8 *)&loadtest_app_config, sizeof(user_app_config_data_t));
		user_app_config_data_valid = 1;
	}
	else {
		debug("APPCONFIG: Test Load Failed !");
	}
		
	return ((result == SPI_FLASH_RESULT_OK) && testLoadValid);
}

bool ICACHE_FLASH_ATTR user_app_config_store() {
	return user_app_config_store_l((uint32*)p_user_app_config_data, sizeof(user_app_config_data_t));
}
