# ESP32-based Room Cooling Control System
This repository contains the source code of ESP32-based room cooling control system project.

## About the project
This project is created for Final Project of Embedded System 1 class. In this project, the control system used is based on ESP32 microcontroller. The system is designed so that the temperature and humidity of a room are maintained in the comfort zone of human and also save the electricity by regularly turning the AC off in "3-hours-on-2-hours-off" manner.

## About the system design
The components used in this system design are:
- ESP32 microcontroller
- DHT22 temperature and humidity sensor
- IR LED emitter
- Simple audio and visual indicator
- Daikin air conditioner

<img src="https://user-images.githubusercontent.com/42486755/184318546-407f4397-97d0-430c-a7e8-c7f55e8eb2fe.png" alt="system-design" width="480" />

The system get the temperature and humidity data from DHT22 sensor. The temperature reading is quite reliable, but the humidity reading is a little bit noisy. To smooth it, Kalman Filter is used.

The system controls a Daikin air conditioner using an IR LED emitter. Parameters that can be controlled are the temperature, mode, fan speed, and swing. In "ON time", if the temperature and humidity are already at comfortable level, *the AC will not turn on* to save electricity.

To ensure robustness, the state of the AC and necessary timing are saved using ESP32 `preferences`. The data reading, state, and timing is uploaded using IoT analytics software ThingSpeak.

## Result
Below is a result from a random measurement.

Monitoring display in ThingSpeak:

<img src="https://user-images.githubusercontent.com/42486755/184325346-1e083eb1-5ccc-4753-ad08-920de5e86a08.png" alt="thingspeak-monitoring" width="480" />

After some processing:

<img src="https://user-images.githubusercontent.com/42486755/184326197-23ee6301-ff25-47ae-ad94-0d7ec6a53e34.png" alt="graph" width="480" />
