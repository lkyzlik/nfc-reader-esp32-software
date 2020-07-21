# NFC Reader ESP32 Software
Software part of the secure NFC card reader developed on the ESP32 platform. My Bachelor thesis project created at Brno University of Technology.

## Other Parts
* Hardware part:

## Requirements
* Hardware part of the project assembled
* [Espressif IoT Development Framework](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
* Text editor

## Install
1. Get [Espressif IoT Development Framework](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) and install it.
2. Build project by running command

    idf.py build

3. Upload program onto ESP32 board by running with correct port name

     idf.py -p (PORT) flash

## Demo Functionality
The reader waits for detection of ISO/IEC 14443A card. When the card is detected, it reads the card's ID and another 32 bytes from its EEPROM memory and sends this data over Wi-Fi to a backend server. The server checks card data against a database and sends back information whether the card owner has access rights. Upon processing the response, the prototype reader signals it to a user with a flash of its indicator LED. Red light for "access denied" or green for "access granted". If the reader is unplugged and the battery charge level is critical, the indicator LED lights up orange and other indications are disabled until the reader is plugged in.

Furthermore, the reader sends in regular intervals of 10 s information about its status to the server. This way, the server continuously verifies that the reader is functional.

All of the communication with the backend server is realized over a secure encrypted HTTPS connection.

## Structure
Project consists of components witch can be used independently.

### NFC Component
NFC component `card_reader_nfc` provides functionality to authenticate ISO/IEC 14443A card and read data from it using a PN532 module and store them in a structured way in the reader memory.

### Wi-Fi Component
Wi-Fi component `card_reader_wifi` provides functionality to configure Wi-Fi, connect to the network and backend server, send card and reader data using secure HTTPS connection and read replies of the server.

### GPIO Component
GPIO component `card_reader_gpio` provides functionality to control onboard and indicator LEDs, and read current power and battery status.

### Main Component
Main component contains main program that controls the reader and realizes functionality described in Demo Functionality. It is also an entry point of the program.
