# Overview
This is the embeded code intended to run on an [M5 Stamp C3](https://docs.m5stack.com/en/core/stamp_c3) Board as part of the Cornhole Game

# IDE Setup
This project uses the Ardunino development enviroment and was tested with 1.8.19,available from <https://www.arduino.cc/en/software>. Once the Arduino IDE is installed the 
configuration to use the [M5 Stamp C3](https://docs.m5stack.com/en/core/stamp_c3) using the following: <https://docs.m5stack.com/en/quick_start/stamp_pico/arduino>

## Dependent Libraries
In order to compile this code, the following libraries must be installed:
- TBA

# MQTT Server
This project requires a MQTT server to be running. Currently the IP address and port are hardcoded

# Game Simulator
The Game Simulator is a python script that mocks the main game. The game simulate mocks the behaviour of the game by:
- Changing the colour of hole one between read, blue and off (changing aevery 5 secs)
- Montoring the ```switch\1``` MQTT topic and printing the message payload when it occurs




