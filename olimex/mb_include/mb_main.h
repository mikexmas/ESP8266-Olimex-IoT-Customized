/* This is base include file for additional code to Olimex IoT project: put it in user_main.c at last position of inncludes */

#ifndef __MB_CONFIG_H__
	#define __MB_CONFIG_H__
		
	/* DEFINITION OF ENABLED/DISABLED DEVICES */
	#define MB_DHT_ENABLE		1		// DHTxx
	#define MB_ADC_ENABLE		0		// ADC (more advanced than original ADC)
	#define MB_PING_ENABLE		0		// PING (distance measurement)
	#define MB_DIO_ENABLE		0		// Digital inputs
	
	#define MB_VARNAMEMAX		12		// max size of varname, including null

	// defines request types for constructing response
	#define MB_REQTYPE_NONE		0	// normal event (eg. time/interrupt)
	#define MB_REQTYPE_GET		1	// we received GET; normally send response measurement(s) of device
	#define MB_REQTYPE_POST		2	// we received post; normally send response CONFIG of device
	#define MB_REQTYPE_SPECIAL	3	// special POST (IFTTT when condistion meeet)
	
	#define MB_POSTTYPE_DEFAULT		0			// Normal behaviour
	#define MB_POSTTYPE_THINGSPEAK	1			// Send measurements to Thingspeak (not other system events allowed)
	#define MB_POSTTYPE_IFTTT		2			// Send to IFTTT when limits exceeded or other condition meet
	#define MB_POSTTYPE_UNSET		255 		// This is actually default FLASH memory value and meand default posttaype
	// ... it may be combined/ THINGSPEAK+IFTTT or normal 

	/* Include once here */
	#include "c_types.h"
	#include "user_interface.h"
	#include "user_webserver.h"
	
	#include "mb_dht.h"
	#include "mb_adc.h"
	#include "mb_ping.h"
	#include "mb_dio.h"
	
	void mb_main();
	
	bool mb_posttype_evaluate();
	
#endif
