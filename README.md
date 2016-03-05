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

Its possible to configure to send data to ThingSpeak.

**DIO**

URL path: /dio

{"Auto": 0, "Dio":0, {"Gpio":5, "Type":30, "Inv":0, "Pls\_on": 2000, "Pls\_off": 0, "Name": "<name>"}, "Start":1}

Auto - autostart when board starts

Dio - id config (0..4)

Gpio - GPIO pin

Type - function:

- 0=NONE
- 10=INPUT NO-PULLUP
- 11=INPU PULLUP
- 12=INPUT PULLUP POSITIVE EDGE
- 13=INPUT NO-PULL POSITIVE EDGE
- 14=INPUT PULLUP NEGATIVE EDGE
- 15=INPUT NOPULL NEGATIVE EDGE
- 30=OUTPUT
- 31=OUTPUT PULSE 

Inv - inverse polarity input / output

Pls\_on - pulse length HIGH in ms

Pls_off - pulse length LOW in ms

Start - make init of HW



**DHTxx**

URL path: /dht

{"Auto":0, "Gpio": 12, "Type": 1, "Refresh": 10000, "Each":5, "Thr_t": 1.0, "Thr_h": 1.0, "Ofs_t": 0.0, "Ofs_h": 0.0, "Units": 0, "Name\_t": "<name>", "Name\_h":"<name>", "Start":1}

Auto - Auto start when board starts

Gpio - Gpio data pin

Type - 0=DHT11, 1=DHT22

Refresh  -reading time in ms

Each - Data is sent when threshold is over, but each n is allways sent

Thr_t - Threshold for T

Thr_h - Threshold for H

Ofs_t - Offset for T

Ofs_h - Offset for H

Units - 0=C, 1=F

Name_t - name of T field for Thingspeak

Name_h - name of H field for Thingspeak

**ADC**

This is upgraded original ADC device provided in user folder, but have some improvements.

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
