# ESP8266-Olimex-IoT-Customized

Based on Olimex's ESP8266 IoT (https://github.com/OLIMEX/ESP8266) with some additional features and drivers.
I try to keep consistency/compatibility with base software and just add features.

Status: it is still in development phase, so some things may change.

OpenHab binding is under developement also, but not ready to publish yet, I am still evaluating and prototyping concept to uniform cover features as much as possible .

All additional devices are customizable via JSON config strings and stored into flash.

**Features:**

- DIO (Digital Input & Outputs, configurable to any valid GPIO, output possible one pulse or repeating pulse)
- DHTxx (driver by https://github.com/eadf, float value, offset, ...)
- PING (measure distance using HCSR04, driver by https://github.com/eadf)
- ADC (customizable, real float value calculation = adc_reading *y + k)
- APP level Settings as addition to user (let's say this system) settings
- float support, values may be float
- actions support

Its possible to configure to send data to ThingSpeak.

**POST TYPES:**

There is standard post type in JSON format, but it is possible to send data to THINGSPEAK or IFTTT beside. Set "Post_type" at meaurement device configuration. Set "Name" also otherwise some default name is given.

0 - standard JSON only
1 - THINGSPEAK (set Name to field1...fieldX)
2 - IFTTT (set Name to some user friendly name)

It is necessary to set IOT server configuration in system configuration /config/iot.

**ACTIONS**

Actions is common way to trigger simple internal actions. Eg-. when temperature limits exceeded, trigger digital output. Set "Action" at meaurement device configuration.

Action:

- 0 - not set
- 1 - DIO 0
- 2 - DIO 1
- 3 - DIO 2
- 4 - DIO 3

**APP-CONFIG**

Application level configuration is implemented to divide system and app configuration.
When measurement devices are configure it is neccessary to invoke POST /app-config {"Save":1} to store settings to flash.


## MEASUREMENT DEVICES ##

**DIO**

URL path: /dio

{"Auto": 0, "Dio":0, {"Gpio":5, "Type":30, "Inv":0, "Pls\_on": 2000, "Pls\_off": 0, "Name": "<name>", "Post_type": o}, "Start":1}

Auto - autostart when board starts

Dio - id config (0..4)

Gpio - GPIO pin

Type - function:

- 0=NONE
- 10=INPUT NO-PULLUP
- 11=INPUT PULLUP
- 12=INPUT PULLUP POSITIVE EDGE
- 13=INPUT NO-PULL POSITIVE EDGE
- 14=INPUT PULLUP NEGATIVE EDGE
- 15=INPUT NOPULL NEGATIVE EDGE
- 20...25 same as 10-15 respective, but 1 second pulse stabilization
- 30=OUTPUT NO PULLUP
- 31=OUTPUT PULLUP

Inv - inverse polarity input / output

Pls\_on - pulse length HIGH in ms (0 no pulse)

Pls_off - pulse length LOW in ms (0 no pulse)

Post_type - see above

Start - make init of HW


**DHTxx**

URL path: /dht

{"Auto":0, "Gpio": 12, "Type": 1, "Refresh": 10000, "Each":5, "Thr_t": 1.0, "Thr_h": 1.0, "Ofs_t": 0.0, "Ofs_h": 0.0, "Units": 0, "Name\_t": "<name>", "Name\_h":"<name>", "Post_type":0, "Low_t": 0.0, "Hi_t": 0.0, "Low_h": 0.0, "Hi_h": 0.0, "Action":0, "Start":1}

Auto - Auto start when board starts

Gpio - Gpio data pin

Type - 0=DHT11, 1=DHT22

Refresh  -reading time in ms

Each - Data is sent when threshold is over, but each n is allways sent when measurement value is changed even less than threshold; 255th measurment is allways sent

Thr_t - Threshold for T

Thr_h - Threshold for H

Ofs_t - Offset for T

Ofs_h - Offset for H

Units - 0=C, 1=F

Name_t - name of T field for Thingspeak

Name_h - name of H field for Thingspeak

Post_type - see above

Low_t/Hi_t - Low and high limits for T for ACTIONS & Post_type(IFTTT) to notify limits crossing

Low_h/Hi_h - Low and high limits for H for ACTIONS & Post_type(IFTTT) to notify limits crossing

Action - see above


**PING - ultrasonic distance measurement HCSR04**

URL path: /ping

{"Auto":0, "Trig_pin": 12, "Echo_pin": 13, "Refresh": 10000, "Each":5, "Max_dist": 2000.0, "Thr": 1.0, "Ofs": 0.0, "Units": 0, "Name": "<name>", "Post_type":0, "Low": 0.0, "Hi": 0.0, "Action":0, "Start":1}

Auto - Auto start when board starts

Trig_pin - Gpio pin to trigger

Echo_pin - Gpio pin to echo

Units - 0=mm, 1=inch

Refresh  -reading time in ms

Each - Data is sent when threshold is over, but each n is allways sent when measurement value is changed even less than threshold; 255th measurment is allways sent

Max_dist - max distance

Thr - Threshold for T

Ofs - Offset for T

Name - name field for Thingspeak, IFTTT

Post_type - see above

Low/Hi - Low and high limits for ACTIONS & Post_type(IFTTT) to notify limits crossing

Action - see above



**ADC**

This is upgraded original ADC device provided in user folder, but have some improvements.

Be aware that ESP8266 valid voltage ADC range is 0..1 VDC.

URL path: /adc

{"Auto":0, "Refresh": 10000, "Each":5, "Thr": 1.0, "ScK": 1.0, "ScY": 0.0, "Name": "<name>", "Start":1}

Auto - Auto start when board starts

Refresh - reading time in ms

Each - Data is sent when threshold is over, but each n is allways sent 

Thr - Threashold in calculated value

ScK - Scale K factor: value = measured * ScK + ScY

ScY - Scale Y factor: value = measured * ScK + ScY

Name - name of value for Thingspeak

Start - make init of HW
