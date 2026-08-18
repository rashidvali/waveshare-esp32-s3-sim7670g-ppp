# Setup

This guide describes how to prepare the hardware and ESP-IDF development environment for the **PPP-over-UART connectivity project for the Waveshare ESP32-S3-SIM7670G-4G development board**.

> **Hardware scope**
>
> This project is developed and tested specifically with the **Waveshare ESP32-S3-SIM7670G-4G development board**.
>
> Other boards using an ESP32-S3 and SIM7670-series modem may have different UART connections, GPIO assignments, modem power-control circuits, or USB configurations and are not currently considered supported by this project.

## 1. Hardware

The primary hardware required is:

- Waveshare ESP32-S3-SIM7670G-4G development board
- Cellular SIM card with an active data plan
- Cellular antenna
- USB cable for programming and serial monitoring
- Computer with ESP-IDF installed

The SIM card must support packet-data service on a cellular network available to the SIM7670G modem.

The APN associated with the SIM will be required when configuring the project.

During development of this project, a **Simbase** SIM has been used with:

```text
APN: simbase
```

The project itself is not tied to Simbase. A different cellular provider can be used by supplying the appropriate APN.

## 2. Waveshare Board

The project communicates with the SIM7670G modem integrated into the **Waveshare ESP32-S3-SIM7670G-4G development board**.

Communication between the ESP32-S3 and SIM7670G uses UART.

The configuration used during development is:

```text
UART:       UART1
ESP32 TX:   GPIO18
ESP32 RX:   GPIO17
Baud rate:  115200
```

These values are specific to the Waveshare board configuration used by this project.

No external UART wiring between the ESP32-S3 and SIM7670G is required when using the supported Waveshare development board.

## 3. Waveshare DIP Switch Configuration

The Waveshare ESP32-S3-SIM7670G-4G development board includes hardware configuration switches labeled:

```text
CAM
HUB
4G
USB
```

The configuration tested with this project is:

```text
CAM: ON
HUB: ON
4G:  ON
USB: ON
```

This all-ON configuration has been successfully tested on multiple Waveshare ESP32-S3-SIM7670G-4G boards using cellular connectivity over PPP.

Unless there is a specific reason to change the board's hardware routing, the recommended starting configuration for this project is therefore:

```text
CAM / HUB / 4G / USB → ON
```

After changing any of these hardware switches, power-cycle or reset the board before troubleshooting software or PPP connectivity.

## 4. Antenna and SIM

Before starting the software:

1. Install the SIM card in the board's SIM card slot.
2. Connect the cellular antenna to the appropriate modem antenna connector.
3. Make sure the antenna is securely connected before attempting cellular communication.
4. Connect the development board to the computer by USB.

Do not assume that successful ESP32-S3 programming means the cellular modem is also ready for communication. The ESP32-S3 and SIM7670G are separate devices on the board and have separate initialization requirements.

## 5. ESP-IDF

The project has been developed and tested with:

```text
ESP-IDF: 5.4.1
Target:  esp32s3
```

ESP-IDF should be installed and its environment initialized before building the project.

Verify the installation with:

```bash
idf.py --version
```

The output should identify the installed ESP-IDF version.

Other ESP-IDF versions may work, but the project is currently developed against **ESP-IDF 5.4.1**.

## 6. Select the ESP32-S3 Target

From the project directory, configure the ESP-IDF target:

```bash
idf.py set-target esp32s3
```

This configures the build system for the ESP32-S3 used by the Waveshare board.

The target normally only needs to be set when creating the build environment or after intentionally resetting the project's configuration.

## 7. Serial Port

Determine which serial port is assigned to the Waveshare board.

On Windows, the port will normally appear as:

```text
COMx
```

For example:

```text
COM5
```

The exact COM port depends on the computer and USB configuration.

The development hardware used for this project has appeared as:

```text
USB-Enhanced-SERIAL CH343 (COM5)
```

This is only an example. Do not assume that another computer will assign the same port number.

## 8. Build the Project

From an ESP-IDF terminal opened in the project directory:

```bash
idf.py build
```

A successful build should complete without compilation or linker errors.

## 9. Flash the ESP32-S3

Flash the application using the serial port assigned to the board.

For example:

```bash
idf.py -p COM5 flash
```

Replace `COM5` with the actual serial port assigned by the operating system.

## 10. Open the Serial Monitor

After flashing, start the ESP-IDF serial monitor:

```bash
idf.py -p COM5 monitor
```

Alternatively, flashing and monitoring can be performed together:

```bash
idf.py -p COM5 flash monitor
```

The default ESP32 console baud rate used by the project is:

```text
115200
```

To exit the ESP-IDF monitor:

```text
Ctrl+]
```

## 11. Verify ESP32-S3 Startup

After reset, the ESP-IDF boot log should appear in the serial monitor.

A normal startup identifies information such as:

```text
ESP32-S3
ESP-IDF version
chip revision
flash configuration
application name
```

The application should then begin initializing the cellular connectivity layer.

At this stage, the goal is only to verify that:

```text
ESP32-S3 boots
        ↓
Application starts
        ↓
UART can be initialized
        ↓
SIM7670G can be contacted
```

PPP connectivity is covered separately.

## 12. Verify Modem Communication

Before PPP can start, the ESP32-S3 must be able to communicate reliably with the SIM7670G using AT commands.

Typical diagnostic commands include:

```text
AT
AT+CPIN?
AT+CSQ
AT+CREG?
AT+CGDCONT?
```

These commands can verify basic modem communication, SIM status, signal information, network registration, and PDP context configuration.

For example:

```text
AT
```

should produce an:

```text
OK
```

response.

The exact responses to the remaining commands depend on the SIM, network, registration state, and cellular provider.

PPP should not be treated as operational until basic AT-command communication with the modem is working.

## 13. What Comes Next

Once the hardware and ESP-IDF environment are working, continue with:

```text
CONFIGURATION.md
```

That document covers the project-specific configuration, including:

- ESP-IDF `menuconfig`
- lwIP PPP support
- PPP debugging options
- TCP/IP task configuration
- UART settings
- APN configuration
- application configuration

The internal PPP-over-UART connection process is documented separately in:

```text
HOW_IT_WORKS.md
```

Problems encountered during modem initialization, dialing, PPP negotiation, IP assignment, or routing are covered in:

```text
TROUBLESHOOTING.md
```

## Supported Environment Summary

```text
Board:       Waveshare ESP32-S3-SIM7670G-4G
MCU:         ESP32-S3
Modem:       SIM7670G
ESP-IDF:     5.4.1
PPP:         lwIP PPPoS
Transport:   UART
UART:        UART1
TX:          GPIO18
RX:          GPIO17
UART baud:   115200
```

The project may eventually be adaptable to other hardware, but the configuration and documentation in this repository should be assumed to apply specifically to the **Waveshare ESP32-S3-SIM7670G-4G development board** unless stated otherwise.