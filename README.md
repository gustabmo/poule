# poule
// poule.ino by guexel@gmail.com
//
// Arduino ESP8266 software to control a chicken coop door
// opens at dawn and closes at dusk
// connects via wifi
// web server with information and manual control of the door
//
// compile in Arduino IDE https://www.arduino.cc/en/Main/Software
// with http://arduino.esp8266.com/stable/package_esp8266com_index.json
//
// components used:
// Arduino board ESP8266 NodeMCU https://shopofthings.ch/shop/iot-module/esp8266-12e-nodemcu-entwicklungsboard-v3/
// motor controller MX1508 (not actually a L298N) https://shopofthings.ch/shop/aktoren/motoren/dual-motor-driver-l298n-motorentreiber/
// photo sensor connected to the ADC https://shopofthings.ch/shop/sensoren/umwelt/photosensor-modul-digital-analog-mit-lm393-schaltung-kompakt/
// reed sensor to identify door open/close https://shopofthings.ch/shop/bauteile/schalter/reedschalter-sensor-magnetischer-schalter-magnetron-modul/
