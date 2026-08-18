# PPP-over-UART Connectivity for the Waveshare ESP32-S3-SIM7670G-4G Development Board

A lightweight ESP-IDF implementation of cellular Internet connectivity using the **SIM7670G modem integrated into the Waveshare ESP32-S3-SIM7670G-4G development board**, with PPP running directly over UART.

The project provides a practical foundation for using the board's cellular modem as an Internet interface for ESP32-S3 applications without relying on `esp_modem` or USB-host modem support.

> **Hardware scope**
>
> This project is developed and tested specifically with the **Waveshare ESP32-S3-SIM7670G-4G development board**.
>
> Other development boards may use the same ESP32-S3 + SIM7670 combination, but their UART connections, modem power control, GPIO assignments, and board-level configuration may differ. They are therefore not currently considered supported by this project.

## Project Goal

The goal is to provide a small, understandable, and reusable PPP-over-UART connectivity layer for applications built on the Waveshare ESP32-S3-SIM7670G-4G board.

The intended architecture is:

```text
Application
    |
    |  TCP/IP sockets, HTTP, MQTT, TLS, etc.
    v
ESP-IDF / lwIP
    |
    v
PPP
    |
    v
UART
    |
    v
SIM7670G
    |
    v
Cellular Network
    |
    v
Internet
```

Once PPP establishes an IP connection, applications should be able to use the standard ESP-IDF networking stack without needing to know that the underlying Internet connection is cellular.

## Hardware

The implementation is currently targeted at:

**Waveshare ESP32-S3-SIM7670G-4G development board**

The board combines:

- ESP32-S3 microcontroller
- SIM7670G cellular modem
- UART communication between ESP32-S3 and SIM7670G
- SIM card interface
- Cellular antenna connection
- USB interfaces for development and debugging

The UART configuration used by this project is:

```text
UART:       UART1
ESP32 TX:   GPIO18
ESP32 RX:   GPIO17
Baud rate:  115200
```

These values correspond to the Waveshare board configuration used during development.

## Software Environment

The project is developed using:

```text
ESP-IDF: 5.4.1
Target:  esp32s3
```

The implementation uses the PPP support provided by ESP-IDF's lwIP stack.

It does **not** require `esp_modem`.

The modem is controlled directly through AT commands over UART, and the same UART is switched to PPP data transport after the modem enters data mode.

## Cellular Connection Flow

The basic connection sequence is:

```text
Initialize ESP-IDF networking
        |
        v
Initialize UART
        |
        v
Communicate with SIM7670G using AT commands
        |
        v
Verify SIM / network state
        |
        v
Configure APN
        |
        v
ATD*99#
        |
        v
CONNECT
        |
        v
Switch UART from AT-command mode to PPP data mode
        |
        v
Start lwIP PPP
        |
        v
Negotiate PPP connection
        |
        v
Obtain IP configuration
        |
        v
Internet connectivity
```

A critical transition occurs after:

```text
ATD*99#
```

when the modem responds:

```text
CONNECT
```

At that point, the UART no longer carries normal AT-command responses. It carries PPP frames between lwIP and the modem.

## APN Configuration

The APN is configurable by the application.

For example:

```c
.apn = "simbase",
```

During development, the project has been tested with a Simbase SIM using:

```text
APN: simbase
```

The library itself should not depend on Simbase; the APN should be configured according to the SIM/provider being used.

## Design Approach

The implementation intentionally keeps the modem and PPP layers relatively small.

The project uses:

- ESP-IDF UART driver
- lwIP PPP/PPPoS
- direct SIM7670G AT commands
- FreeRTOS tasks
- standard ESP-IDF networking APIs

It does not introduce an additional modem abstraction framework between the application and lwIP.

This makes the PPP startup process easier to inspect and is particularly useful when debugging modem registration, dialing, PPP negotiation, IP assignment, and routing.

## Repository Documentation

Detailed documentation is organized into separate files:

- `SETUP.md` — hardware and development environment setup
- `CONFIGURATION.md` — ESP-IDF, `menuconfig`, UART, APN, and PPP configuration
- `HOW_IT_WORKS.md` — internal architecture and PPP-over-UART connection flow
- `TROUBLESHOOTING.md` — modem, UART, dialing, PPP, IP, and routing troubleshooting

## Current Status

The project is under active development.

The original implementation successfully established PPP connectivity with the SIM7670G modem on the Waveshare ESP32-S3-SIM7670G-4G board.

The code is being reorganized into a cleaner and more reusable connectivity component while preserving the behavior of the proven implementation.

Because of this ongoing refactoring, the repository should currently be considered a development project rather than a finished production-ready library.

## Intended Use

The project is intended as a cellular connectivity foundation for ESP32-S3 applications that need Internet access through the SIM7670G modem.

Once PPP connectivity is established, higher-level ESP-IDF functionality can use the resulting network interface, including applications such as:

```text
HTTP / HTTPS
MQTT / MQTTS
REST APIs
OTA updates
Cloud connectivity
TCP/UDP sockets
DNS
SNTP
```

These services are intentionally outside the core responsibility of the PPP-over-UART layer.

The library's primary responsibility is:

```text
Waveshare ESP32-S3-SIM7670G-4G
              +
        Cellular network
              ↓
        IP connectivity
```

## License

License information will be provided with the project.