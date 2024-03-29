# IthoEcoFanRFT
Cloned from videgro, modified to intergrate some separate projects
   
* Added webserver code/api, mostly a copied from incmve https://github.com/incmve/Itho-WIFI-remote
* Added MQTT, loosly copied from incmve
* Added Leave, Timer2 and Timer3


Will work with a 868MHz CC1101 module.
The CC1150 may also work, except for receiving (which is not required for controlling an Itho EcoFan).
A 433MHz CC1101/CC1150 might also work, because it has the same chip. But a 433MHz CC11xx board has a different RF filter, causing a lot less transmission power (and reception).
```
Connections between the CC1101 and the ESP8266 or Arduino:
CC11xx pins    ESP pins Arduino pins  Description
*  1 - VCC        VCC      VCC           3v3
*  2 - GND        GND      GND           Ground
*  3 - MOSI       13=D7    Pin 11        Data input to CC11xx
*  4 - SCK        14=D5    Pin 13        Clock pin
*  5 - MISO/GDO1  12=D6    Pin 12        Data output from CC11xx / serial clock from CC11xx
*  6 - GDO2       04=D2    Pin  2        Data receive interrupt pin CC11xx
*  7 - GDO0       ?        Pin  ?        Programmable output
*  8 - CSN        15=D8    Pin 10        Chip select / (SPI_SS)
```
Note that CC11xx pin GDO0 is not used.
Also note that the GDO2 pin is connected to a pin on an Arduino/ESP8266. Change ```#define ITHO_IRQ_PIN``` in the example ino accordingly.

You should keep the wires to the CC11xx module as short as possible.

Beware that the CC11xx modules are 3.3V (3.6V max) on all pins!
This won't be a problem with an ESP8266, but for Arduino you either need to use a 3.3V Arduino or use levelshifters and a separate 3.3V power source.

For use with an ESP8266, you will need the ESP8266 core for Arduino from https://github.com/esp8266/Arduino

For SPI, pins 12-15 (aka D5-D8) are used. 
A larger ESP8266 model like (but not only) the ESP-03, ESP-07, ESP-12(D, E) or a NodeMCU board is required.
