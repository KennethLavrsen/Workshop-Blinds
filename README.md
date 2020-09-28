# Workshop-Blinds
Arduino program for ESP8266 for controlling stepper motor blinds

The program is written for a D1-mini with a simple stepper driver

The blinds are controlled by MQTT

All the secrets are stored in secrets.h

Other settings are in the header of the .ino file

MQTT topics

- mqttTopicAnnounce  "workshop-blinds/announce" Blinds controller announces itself at boot
- mqttTopicSet       "workshop-blinds/set"      Set the blinds position 0-100%
- mqttTopicStep      "workshop-blinds/step"     Step the blinds. Range -100 to +100
- mqttTopicPosition  "workshop-blinds/position" Blinds controller reports its current position in range 0-100%
