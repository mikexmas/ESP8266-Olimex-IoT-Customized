#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "ip_addr.h"
#include "mem.h"
#include "base64.h"

#include "user_interface.h"

#include "user_json.h"
#include "user_timer.h"
#include "user_events.h"
#include "user_config.h"
#include "user_webserver.h"
#include "user_webclient.h"
#include "user_long_poll.h"

#include "user_badge.h"

void ICACHE_FLASH_ATTR user_event_server_error() {
// TODO - register callbacks to break module/device dependency
#if DEVICE == BADGE
	badge_server_animation_start();
#endif
}

void ICACHE_FLASH_ATTR user_event_server_ok() {
// TODO - register callbacks to break module/device dependency
#if DEVICE == BADGE
	badge_server_animation_stop();
#endif
}

void ICACHE_FLASH_ATTR user_event_reboot() {
#if EVENTS_DEBUG
	debug("EVENTS: Reboot\n");
#endif
	char status[WEBSERVER_MAX_VALUE];
	user_event_raise(NULL, json_status(status, ESP8266, REBOOTING, NULL));
	websocket_close_all(REBOOTING, NULL);
	long_poll_close_all();
}

void ICACHE_FLASH_ATTR user_event_connect() {
#if EVENTS_DEBUG
	debug("EVENTS: Station connected\n");
	// memory_info();
#endif
	char status[WEBSERVER_MAX_RESPONSE_LEN];
	user_event_raise(
		USER_CONFIG_STATION_URL, 
		json_data(status, ESP8266, CONNECTED, (char *)config_wifi_station(), NULL)
	);
}

void ICACHE_FLASH_ATTR user_event_reconnect() {
#if EVENTS_DEBUG
	debug("EVENTS: Reconnect station\n");
#endif
	char status[WEBSERVER_MAX_VALUE];
	user_event_raise(NULL, json_status(status, ESP8266, RECONNECT, NULL));
	websocket_close_all(RECONNECT, NULL);
	long_poll_close_all();
}

LOCAL void ICACHE_FLASH_ATTR user_event_wifi(System_Event_t *evt) {
	switch (evt->event) {
		case EVENT_STAMODE_CONNECTED:
		break;
		
		case EVENT_STAMODE_DISCONNECTED:
// TODO - register callbacks to break module/device dependency
#if DEVICE == BADGE
			badge_wifi_animation_start();
#endif
#if DEVICE == PLUG
			plug_wifi_blink_start();
#endif
			wifi_auto_detect();
		break;
		
		case EVENT_STAMODE_AUTHMODE_CHANGE:
		break;
		
		case EVENT_STAMODE_DHCP_TIMEOUT:
		break;
		
		case EVENT_STAMODE_GOT_IP:
// TODO - register callbacks to break module/device dependency
#if DEVICE == BADGE
			badge_wifi_animation_stop();
#endif
#if DEVICE == PLUG
			plug_wifi_blink_stop();
#endif
			user_event_connect();
		break;
		
		case EVENT_SOFTAPMODE_STACONNECTED:
		break;
		
		case EVENT_SOFTAPMODE_STADISCONNECTED:
		break;
	}
}

void ICACHE_FLASH_ATTR user_event_build(char *event, char *url, char *data) {
	char url_quote[2] = "\"";
	
	if (url == NULL) {
		url_quote[0] = '\0';
	}
	
	os_sprintf(
		event,
		"{\"EventURL\" : %s%s%s, \"EventData\" : %s}",
		url_quote,
		url == NULL ? "null" : url,
		url_quote,
		data
	);
}

void ICACHE_FLASH_ATTR user_event_progress(uint8 progress) {
	LOCAL uint8 prev = 0;
	
	uint32 heap = system_get_free_heap_size();
	if (abs(progress - prev) < 3 || heap < 5000) {
		return;
	}
	
	prev = progress;
	char status[WEBSERVER_MAX_VALUE];
	char progress_str[WEBSERVER_MAX_VALUE];
	json_status(status, ESP8266, json_sprintf(progress_str, "Progress: %d\%", progress), NULL);
	
#if EVENTS_DEBUG
	debug("EVENTS: Progress [%d]\n", progress);
#endif
	char event[WEBSERVER_MAX_VALUE + os_strlen(webserver_chunk_url()) + os_strlen(status)];
	
	user_event_build(event, webserver_chunk_url(), status);
	websocket_send_message(EVENTS_URL, event, NULL);
}

void ICACHE_FLASH_ATTR user_websocket_event(char *url, char *data, struct espconn *pConnection) {
	char event[WEBSERVER_MAX_VALUE + os_strlen(url) + os_strlen(data)];
	
	user_event_build(event, url, data);
	
	websocket_send_message(url, data, pConnection);
	websocket_send_message(EVENTS_URL, event, pConnection);
}

void ICACHE_FLASH_ATTR user_event_raise(char *url, char *data) {
	if (data == NULL || os_strlen(data) == 0) {
		return;
	}
	
	uint16 event_size = 
		WEBSERVER_MAX_VALUE + 
		(url == NULL ? 0 : os_strlen(url)) + 
		os_strlen(data)
	;
	
	char *event = (char *)os_malloc(event_size);
	if (event == NULL) {
#if EVENTS_DEBUG
		debug("EVENTS: Cannot allocate memory\n");
#endif
		return;
	}
	char *pevent = event;
	user_event_build(event, url, data);
	
	if (url == NULL) {
		websocket_send_message(EVENTS_URL, data, NULL);
		long_poll_response(EVENTS_URL, data);
	} else {
		// WebSockets
		websocket_send_message(url, data, NULL);
		websocket_send_message(EVENTS_URL, event, NULL);
		
		// Long Polls
		long_poll_response(url, data);
		long_poll_response(EVENTS_URL, event);
	}
	
	// MihaB: SEND to IoT server bare data and not system events; TODO BETTER handling of forbidden events
	if (USER_CONFIG_EVENTS_FORMAT_THINGSPEAK == user_config_events_post_format()) {
		// all system data begins with device; do not make 1st json field Device for Thingspeak server
		char* ret = (char*)os_strstr(data, "Device");
		if ((ret != NULL) && (ret - data < 5)) {
			pevent = (char*)NULL;
		}
		else {
			pevent = data;
		}
	}
	// End MihaB

	// POST to IoT server
	if (pevent != NULL) {
		if (user_config_events_websocket()) {
			webclient_socket(
				user_config_events_ssl(),
				user_config_events_user(),
				user_config_events_password(),
				user_config_events_server(),
				user_config_events_ssl() ?
					WEBSERVER_SSL_PORT
					:
					WEBSERVER_PORT
				,
				user_config_events_path(),
				pevent
			);
		} else {
			webclient_post(
				user_config_events_ssl(),
				user_config_events_user(),
				user_config_events_password(),
				user_config_events_server(),
				user_config_events_ssl() ?
					WEBSERVER_SSL_PORT
					:
					WEBSERVER_PORT
				,
				user_config_events_path(),
				pevent
			);
		}
	}
	os_free(event);
}

void ICACHE_FLASH_ATTR events_handler(
	struct espconn *pConnection, 
	request_method method, 
	char *url, 
	char *data, 
	uint16 data_len, 
	uint32 content_len, 
	char *response,
	uint16 response_len
) {
	webserver_set_status(0);
}

void ICACHE_FLASH_ATTR user_events_init() {
	wifi_set_event_handler_cb(user_event_wifi);
	webserver_register_handler_callback(EVENTS_URL, events_handler);
}