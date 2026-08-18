# Configuration

This document describes the software configuration used for PPP-over-UART connectivity on the **Waveshare ESP32-S3-SIM7670G-4G development board**.

> **Hardware scope**
>
> The settings documented here correspond to the Waveshare ESP32-S3-SIM7670G-4G development board used and tested by this project.
>
> Other ESP32-S3 + SIM7670 hardware combinations may require different UART, GPIO, modem, or board-level configuration.

## 1. ESP-IDF Environment

The project has been developed and tested with:

```text
ESP-IDF: 5.4.1
Target:  esp32s3
```

Set the target from the project directory with:

```bash
idf.py set-target esp32s3
```

Configuration can then be opened with:

```bash
idf.py menuconfig
```

## 2. lwIP PPP Support

PPP support must be enabled in ESP-IDF.

Navigate to:

```text
Component config
    → LWIP
        → PPP support
```

Enable:

```text
[*] PPP support
```

This enables the lwIP PPP implementation used by the project.

The project uses PPP over a serial connection, commonly referred to as **PPPoS — PPP over Serial**.

The resulting data path is:

```text
lwIP
  ↓
PPP / PPPoS
  ↓
ESP32-S3 UART
  ↓
SIM7670G
  ↓
Cellular network
```

## 3. TCP/IP Task Stack Size

During development, the lwIP TCP/IP task stack size was increased.

Navigate to:

```text
Component config
    → LWIP
        → TCP/IP Task Stack Size
```

The original value was:

```text
3072
```

The configuration used by this project is:

```text
8192
```

Therefore:

```text
(8192) TCP/IP Task Stack Size
```

The larger stack provides additional headroom while running PPP and while detailed lwIP/PPP diagnostic logging is enabled.

## 4. PPP Debug Logging

PPP debugging was enabled during development and troubleshooting.

Navigate to:

```text
Component config
    → LWIP
        → PPP support
```

Enable:

```text
[*] Enable PPP debug log output
```

This setting is useful for examining PPP protocol negotiation, including stages such as:

```text
LCP
IPCP
PPP state transitions
connection termination
```

### Is PPP Debug Logging Required?

No.

PPP debug output is a **development and troubleshooting setting**, not a fundamental requirement for PPP-over-UART operation.

For development builds, enabling it is useful.

For production builds, it can be disabled after PPP operation has been verified.

## 5. Route lwIP Debugging Through ESP_LOG

During development, lwIP debugging was also configured to use the ESP-IDF logging infrastructure.

Under:

```text
Component config
    → LWIP
```

enable:

```text
[*] Route LWIP debugs through ESP_LOG interface
```

This allows lwIP diagnostic output to appear through the normal ESP-IDF logging system.

Like PPP debug logging, this is primarily a troubleshooting feature and is not inherently required for normal PPP operation.

## 6. UART Configuration

The SIM7670G modem on the supported Waveshare board is accessed using:

```text
UART:       UART1
TX GPIO:    GPIO18
RX GPIO:    GPIO17
Baud rate:  115200
```

The application configuration therefore uses values equivalent to:

```c
.uart_num = UART_NUM_1,
.tx_gpio = 18,
.rx_gpio = 17,
.baud = 115200,
```

These GPIO assignments correspond to the **Waveshare ESP32-S3-SIM7670G-4G development board** configuration used and tested by this project.

Do not assume that the same GPIO assignments apply to another ESP32-S3 + SIM7670 board.

## 7. UART Operating Modes

The same UART connection is used for two fundamentally different purposes during connection establishment.

Initially it carries SIM7670G AT commands:

```text
ESP32-S3
    |
    | AT commands
    v
SIM7670G
```

For example:

```text
AT
AT+CPIN?
AT+CSQ
AT+CREG?
AT+CGDCONT?
ATD*99#
```

After the modem enters data mode, the UART carries PPP frames:

```text
ESP32-S3 lwIP
      |
      | PPP frames
      v
    UART1
      |
      v
   SIM7670G
```

The transition between these modes is an important part of the implementation and is described in more detail in `HOW_IT_WORKS.md`.

## 8. APN Configuration

The cellular provider's APN must be supplied to the connectivity layer.

The configuration structure contains an APN field:

```c
.apn = "simbase",
```

During development and testing, the project has used a Simbase SIM with:

```text
APN: simbase
```

The project itself is not tied to Simbase.

For another cellular provider, replace the APN with the value required by that provider.

For example:

```c
.apn = "your-apn",
```

The modem's PDP context can be configured using a command equivalent to:

```text
AT+CGDCONT=1,"IP","simbase"
```

For another provider:

```text
AT+CGDCONT=1,"IP","your-apn"
```

## 9. Dial Command

The PPP data connection is initiated using:

```text
ATD*99#
```

The application configuration therefore contains:

```c
.dial_cmd = "ATD*99#",
```

When dialing succeeds, the modem should enter data mode.

The expected transition is:

```text
ATD*99#
    ↓
CONNECT
    ↓
PPP data mode
```

After this transition, normal AT commands must not be sent over the UART while the PPP session is active.

## 10. Application Configuration

A typical configuration for the Waveshare board looks like:

```c
sim7670_ppp_config_t cfg = {
    .uart_num = UART_NUM_1,
    .tx_gpio = 18,
    .rx_gpio = 17,
    .baud = 115200,

    .apn = "simbase",
    .dial_cmd = "ATD*99#",
    .do_at_probe = true,

    .rx_task_stack = 4096,
    .rx_task_prio = 10,
    .modem_task_stack = 4096,
    .modem_task_prio = 8,

    .auto_reconnect = false,
    .reconnect_delay_ms = 5000,

    .cb = ppp_evt,
    .cb_user_ctx = NULL
};
```

The UART configuration shown above corresponds to the supported Waveshare board.

The APN must be changed when using a cellular provider that requires a different APN.

## 11. ESP-IDF Network Initialization

Before starting the PPP connectivity layer, the application initializes the ESP-IDF networking infrastructure.

The application startup includes:

```c
ESP_ERROR_CHECK(nvs_flash_init());
ESP_ERROR_CHECK(esp_netif_init());

esp_err_t e = esp_event_loop_create_default();

if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(e);
}
```

The PPP component can then be initialized and started:

```c
ESP_ERROR_CHECK(sim7670_ppp_init(&cfg));
ESP_ERROR_CHECK(sim7670_ppp_start());
```

This ordering ensures that the ESP-IDF networking infrastructure exists before the PPP connectivity component begins operation.

## 12. Modem Task Configuration

The project uses FreeRTOS tasks for modem management and UART PPP reception.

The example application configuration uses:

```text
PPP RX task stack:       4096
PPP RX task priority:    10

Modem task stack:        4096
Modem task priority:     8
```

Corresponding configuration:

```c
.rx_task_stack = 4096,
.rx_task_prio = 10,

.modem_task_stack = 4096,
.modem_task_prio = 8,
```

These are the values used by the current project configuration.

They should not be confused with:

```text
TCP/IP Task Stack Size = 8192
```

The TCP/IP task is part of lwIP, while the modem and PPP UART RX tasks belong to this project.

## 13. Automatic Reconnection

The configuration structure provides settings for reconnect behavior:

```c
.auto_reconnect = false,
.reconnect_delay_ms = 5000,
```

Automatic reconnection is currently disabled in the example configuration.

Reconnect behavior can be developed independently of the fundamental PPP connection sequence.

This helps keep initial modem bring-up and PPP troubleshooting deterministic.

## 14. Board Switch Configuration

The tested Waveshare board configuration uses all four hardware switches in the ON position:

```text
CAM: ON
HUB: ON
4G:  ON
USB: ON
```

Or, more compactly:

```text
CAM / HUB / 4G / USB → ON
```

This configuration has been successfully used with the Waveshare ESP32-S3-SIM7670G-4G boards running the cellular firmware and PPP connectivity implementation.

The board switch configuration is also documented in `SETUP.md`.

## 15. Configuration Summary

The known development configuration can be summarized as:

```text
Hardware
--------
Board:                Waveshare ESP32-S3-SIM7670G-4G
MCU:                  ESP32-S3
Modem:                SIM7670G

ESP-IDF
-------
ESP-IDF:              5.4.1
Target:               esp32s3

UART
----
UART:                 UART1
TX:                   GPIO18
RX:                   GPIO17
Baud:                 115200

Cellular
--------
Example provider:      Simbase
Example APN:           simbase
Dial command:          ATD*99#

lwIP
-----
PPP support:           Enabled
TCP/IP task stack:     8192

Development Debugging
---------------------
PPP debug output:      Enabled
lwIP via ESP_LOG:      Enabled

Waveshare Switches
------------------
CAM:                   ON
HUB:                   ON
4G:                    ON
USB:                   ON
```

## Required vs. Development Settings

The configuration can broadly be divided into two categories.

### Core Connectivity Configuration

These settings are part of establishing the cellular PPP connection:

```text
ESP32-S3 target
PPP support
UART1
GPIO18 / GPIO17
115200 baud
APN
ATD*99#
```

### Development / Diagnostic Configuration

These settings were enabled to make PPP development and troubleshooting easier:

```text
PPP debug log output
Route lwIP debugging through ESP_LOG
TCP/IP task stack increased to 8192
```

The debug logging options can eventually be disabled for production builds once the PPP implementation is stable.

The increased TCP/IP task stack should be evaluated separately before reducing it; it should not automatically be returned to the ESP-IDF default simply because debug logging has been disabled.

## Next

For an explanation of what happens internally when the application connects to the cellular network, see:

```text
HOW_IT_WORKS.md
```

For problems involving AT commands, network registration, `ATD*99#`, `CONNECT`, PPP negotiation, IP assignment, or routing, see:

```text
TROUBLESHOOTING.md
```