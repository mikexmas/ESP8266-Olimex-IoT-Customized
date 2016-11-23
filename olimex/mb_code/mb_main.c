#include "mb_main.h"

#include "osapi.h"

mb_interrupt_t mb_interrupts[MB_INTERRUPT_MAX_NUM];

// GPIO interrupt handler
LOCAL void mb_main_intr_handler(void *arg) {
	int i=0;
	mb_interrupt_t *pcurInt = (mb_interrupt_t *)NULL;
	for (i;i<MB_INTERRUPT_MAX_NUM;i++) {
		pcurInt = &mb_interrupts[i];
		if ((GPIO_REG_READ(GPIO_STATUS_ADDRESS) & BIT(pcurInt->pin)) && pcurInt->interruptHandler) {
			(pcurInt->interruptHandler(arg));
		}
	}
}

void ICACHE_FLASH_ATTR mb_main() {

	// init itnerruots pointers
	int i=0;
	for (i=0;i<MB_INTERRUPT_MAX_NUM;i++) {
		mb_interrupt_t *pcurInt = &mb_interrupts[i];
		pcurInt->pin = 0xFF;
		pcurInt->interruptHandler = (void*)NULL;
	}

	user_app_config_init();

#if MB_DHT_ENABLE
	mb_dht_init(false);
#endif

#if MB_AIN_ENABLE
	mb_ain_init(false);
#endif
	
#if MB_PING_ENABLE
	mb_ping_init(false);
#endif

#if MB_DIO_ENABLE
	mb_dio_init();
#endif

	/* Show us intr */
	int cnt = 0;
	for (i=0;i<MB_INTERRUPT_MAX_NUM;i++) {
		mb_interrupt_t *pcurInt = &mb_interrupts[i];
		if (pcurInt->pin != 0xFF) {
			cnt++;
			os_printf("Interrupt: %d %d\n", pcurInt->pin, pcurInt->interruptHandler);
		}
	}
	if (cnt) {
		ETS_GPIO_INTR_ATTACH(mb_main_intr_handler, NULL);
	}
}

/* Interrupts */
void mb_intr_add(uint8 pin, void (*interruptHandler)(void *arg))
{
	os_printf("mb_intr_add:%d %d\n", pin, interruptHandler);
	
	int i=0;
	bool is_found = false;
	mb_interrupt_t *pcurInt = (mb_interrupt_t *)NULL;
	// check if allready exists
	for (i=0;i<MB_INTERRUPT_MAX_NUM;i++) {
		pcurInt = &mb_interrupts[i];
		if (pcurInt->pin == pin) {
			is_found = true;
			break;
		}
	}
	if (!is_found) {
		for (i=0;i<MB_INTERRUPT_MAX_NUM;i++) {
			pcurInt = &mb_interrupts[i];
			if (pcurInt->pin == 0xFF) {
				pcurInt->pin = pin;
				pcurInt->interruptHandler = interruptHandler;
				break;
			}
		}
	}
};

#if MB_ACTIONS_ENABLE
/* Actions triggering from other; call make using setTimeout */
void ICACHE_FLASH_ATTR mb_action_post(mb_action_data_t *p_act_data) {
	uint8 action_type = MB_ACTIONTYPE_NONE;
	char data[WEBSERVER_MAX_VALUE];
	if (p_act_data != NULL) {
		action_type = p_act_data->action_type;
	}
#if MB_DIO_ENABLE
	if (action_type >= MB_ACTIONTYPE_DIO_0 && action_type <= MB_ACTIONTYPE_DIO_LAST) {
		uint8 dio_id = action_type - MB_ACTIONTYPE_DIO_0;
		os_sprintf(data, "{\"Output%d\": %d}", dio_id, p_act_data->value);
		if (action_type >= MB_ACTIONTYPE_DIO_0 && action_type <= MB_ACTIONTYPE_DIO_LAST) {
			mb_dio_handler(NULL, POST, MB_DIO_URL, data, os_strlen(data), os_strlen(data), NULL, 0);
		}
	}
#endif
}
#endif
