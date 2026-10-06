# BryantHP ESP32-S3 connections

ESP32-S3-N16R8. GPIO 26–37 are inside the module on flash and octal PSRAM. Leave GPIO 0, 3, 45, and 46 alone (boot straps). GPIO 19 and 20 are the USB connector.

| ESP pin | Signal | Connects to |
|---|---|---|
| 5V / VIN | 5 VDC | Buck converter output |
| GND | Ground | Buck ground, ABCD C, AHT GND, BMP GND, OLED GND, SD GND |
| 3V3 | 3.3 V | AHT VCC, BMP VCC, OLED VCC, SD VCC |
| GPIO 16 | UART RX | RS485 receiver output (RO) |
| GPIO 8 | I2C SDA | AHT SDA, BMP SDA, OLED SDA (0x3C) |
| GPIO 9 | I2C SCL | AHT SCL, BMP SCL, OLED SCL |
| GPIO 39 | SD clock | SD SCK / CLK |
| GPIO 38 | SD data in | SD MISO / DO |
| GPIO 40 | SD data out | SD MOSI / DI |
| GPIO 41 | SD chip select | SD CS |
| GPIO 48 | Onboard RGB LED | On the module. Not used. Do not connect a wire |

No ESP pin is a UART transmit pin. The firmware does not write the Evolution bus.

These ABCD wires do not land on an ESP GPIO:

| ABCD | Connects to |
|---|---|
| A+ | RS485 non-inverting data (A) |
| B− | RS485 inverting data (B) |
| C | Ground, shared with the buck and the ESP |
| D | 24 VAC into the buck converter |
