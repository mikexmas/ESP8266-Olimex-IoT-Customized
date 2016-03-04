/* This is base include file for additional code to Olimex IoT project: put it in user_main.c at last position of inncludes */

#ifndef __MB_CONFIG_H__
	#define __MB_CONFIG_H__
		
	/* DEFINITION OF ENABLED/DISABLED DEVICES */
	#define DHT_ENABLE		1		// DHTxx
	#define MB_ADC_ENABLE	1		// ADC (more advanced than original ADC)
	#define PING_ENABLE		1		// PING (distance measurement)
	#define MB_DIO_ENABLE	1		// Digital inputs
	
	#define MB_VARNAMEMAX		12		// max size of varname, including null
	
	/* Include once here */
	#include "c_types.h"
	#include "user_interface.h"
	#include "user_webserver.h"
	
	#include "mb_dht.h"
	#include "mb_adc.h"
	#include "mb_ping.h"
	#include "mb_dio.h"
	
	void mb_main();
	
#endif
