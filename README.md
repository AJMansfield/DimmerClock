# DimmerClock
Software for my alarm clock dimmer project.


## Goals for the Device
The device is intended to be an all-purpose time-controlled mains dimmer.
The device is intended to be stand-alone - anything that one might want to configure on it should be configurable from its own control panel.
The device must tolerate random power cuts, resets, and being moved around - all persistent state needs to be stored in non-volatile memory, and needs a hardware RTC to allow it to keep time even when powered off.
The device should - at least in theory - work regardless of mains voltage/frequency.


## Hardware Currently in Use
An Arduino Uno
An I2C serial LCD module, 2 lines by 16 characters, address 0x3f
An I2C DS1307 RTC module with battery backup, address 0x68
An I2C AT24C32 32K EEPROM, address 0x50
A 2-channel triac dimmer board - sync is connected to arduino pin 8, channels A and B to 9 and 10
A rotary encoder with pushbutton - common to ground, encoder poles to arduino pin 7 and 6, button to 5

A mains power to 5VDC power supply that powers everything (so the device can operate standalone)
An ISO320 / C14 power connector - for the power supply and dimmer
Two NEMA 15 power outlets for the two output channels
