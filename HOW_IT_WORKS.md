# How It Works

This document explains how PPP-over-UART cellular connectivity works on the **Waveshare ESP32-S3-SIM7670G-4G development board** and how the project connects the ESP32-S3 networking stack to the Internet through the onboard SIM7670G modem.

> **Hardware scope**
>
> This implementation is developed and tested specifically with the **Waveshare ESP32-S3-SIM7670G-4G development board**.
>
> The general PPP concepts described here apply more broadly, but the UART connections and board-level configuration documented by this project are specific to the supported Waveshare board.

## 1. Architecture

The project creates an IP network connection through the SIM7670G cellular modem using PPP over a serial connection.

The complete data path is:

```text
Application
    |
    | HTTP, HTTPS, MQTT, TLS,
    | TCP/UDP sockets, DNS, etc.
    v
ESP-IDF Networking
    |
    v
lwIP TCP/IP Stack
    |
    v
PPP / PPPoS
    |
    v
UART1
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

The key design principle is that the cellular modem provides the transport for an ordinary IP network interface.

Once PPP is established, higher-level application code can use the standard ESP-IDF networking APIs without implementing cellular-specific versions of HTTP, MQTT, TCP, or other Internet protocols.

## 2. Why PPP over UART?

The SIM7670G can communicate with the ESP32-S3 through UART.

Before an Internet connection is established, UART is used for modem control through AT commands.

After dialing succeeds, the same UART connection becomes a binary PPP data channel.

This allows the ESP32-S3's lwIP stack to negotiate and operate an IP connection through the modem.

This project therefore does not use the modem's own HTTP or MQTT AT-command implementations as the primary application networking layer.

Instead:

```text
SIM7670G
    =
cellular transport for the ESP32-S3 IP stack
```

The ESP32-S3 remains responsible for the application's networking protocols.

## 3. Two UART Operating Modes

One of the most important concepts in the implementation is that UART has two distinct operating modes.

### AT-Command Mode

Initially, communication is text based.

The ESP32-S3 sends commands such as:

```text
AT
AT+CPIN?
AT+CSQ
AT+CREG?
AT+CGDCONT?
```

and receives modem responses.

Conceptually:

```text
ESP32-S3
    |
    | "AT\r\n"
    v
SIM7670G
    |
    | "OK"
    v
ESP32-S3
```

During this phase, the application can inspect the modem, SIM, cellular registration, and PDP context.

### PPP Data Mode

After dialing succeeds, UART stops being an ordinary AT-command channel.

Instead, it carries binary PPP frames:

```text
lwIP PPP
    |
    | binary PPP frames
    v
UART1
    |
    v
SIM7670G
```

It is critical that these two modes are not mixed.

Sending normal AT commands into the UART while PPP owns the data channel can corrupt the PPP session.

## 4. Modem Preflight

Before dialing, the application verifies that communication with the modem is available.

Typical commands include:

```text
AT
AT+CPIN?
AT+CSQ
AT+CREG?
AT+CGDCONT?
```

These commands serve different diagnostic purposes.

### `AT`

Verifies basic UART communication with the modem.

Expected successful response:

```text
OK
```

### `AT+CPIN?`

Checks SIM status.

A ready SIM normally reports:

```text
+CPIN: READY
```

### `AT+CSQ`

Provides cellular signal information.

This is useful during diagnostics because successful UART communication does not necessarily mean that the modem has usable cellular service.

### `AT+CREG?`

Provides network registration information.

The modem must be registered with a cellular network before a usable data connection can normally be established.

### `AT+CGDCONT?`

Displays the configured PDP contexts.

This is useful for verifying the APN and associated packet-data configuration.

## 5. APN Configuration

Before dialing, the modem needs the appropriate APN for the cellular provider.

The project can configure PDP context 1 using a command equivalent to:

```text
AT+CGDCONT=1,"IP","simbase"
```

For the Simbase SIM used during development:

```text
APN: simbase
```

For another provider:

```text
AT+CGDCONT=1,"IP","your-apn"
```

The APN is therefore application configuration rather than a hard-coded requirement of the PPP implementation.

## 6. Dialing

The transition from modem-control mode to PPP data mode begins with:

```text
ATD*99#
```

The configured project value is:

```c
.dial_cmd = "ATD*99#",
```

The important sequence is:

```text
AT-command mode
      |
      v
   ATD*99#
      |
      v
    CONNECT
      |
      v
PPP data mode
```

`CONNECT` is not an indication that PPP negotiation itself has finished.

It indicates that the modem has entered the data connection mode in which PPP communication can begin.

This distinction is important:

```text
CONNECT
   !=
PPP has an IP address
```

Instead:

```text
CONNECT
   =
UART is ready to carry PPP
```

## 7. Starting the PPP Stack

After the modem has entered data mode, the project starts lwIP PPP.

The PPP implementation used by the project is PPPoS:

```text
PPP over Serial
```

The PPP control block connects lwIP to two directions of serial traffic:

```text
                TX
lwIP PPP ----------------> UART1
                            |
                            v
                         SIM7670G

                RX
lwIP PPP <---------------- UART1
                            ^
                            |
                         SIM7670G
```

Both directions are required.

PPP cannot negotiate successfully if either transmit or receive traffic is missing.

## 8. PPP Transmit Path

When lwIP needs to transmit PPP data, PPPoS produces a PPP frame.

The project's PPP output callback sends those bytes to the SIM7670G through UART.

Conceptually:

```text
lwIP
  |
  v
PPP protocol
  |
  v
PPPoS output callback
  |
  v
uart_write_bytes(...)
  |
  v
UART1
  |
  v
SIM7670G
```

The data at this point is binary PPP traffic, not AT-command text.

The modem forwards the resulting network traffic through the cellular connection.

## 9. PPP Receive Path

Data arriving from the cellular connection follows the opposite path.

The SIM7670G places PPP bytes onto UART.

A UART receive task reads those bytes and passes them into lwIP PPP.

Conceptually:

```text
SIM7670G
    |
    v
UART1
    |
    v
UART RX task
    |
    v
PPPoS input
    |
    v
lwIP PPP
    |
    v
TCP/IP stack
```

The UART receive task does not interpret the incoming PPP frames itself.

Its job is primarily to transport received bytes from UART into the PPP parser.

lwIP handles the PPP protocol.

## 10. PPP Negotiation

Once both sides begin exchanging PPP frames, PPP negotiation starts.

The process includes several protocol stages.

A simplified sequence is:

```text
CONNECT
   |
   v
LCP negotiation
   |
   v
Optional authentication
   |
   v
IPCP negotiation
   |
   v
IP configuration
   |
   v
PPP network interface operational
```

### LCP

LCP stands for:

```text
Link Control Protocol
```

LCP establishes and configures the basic PPP link.

The peers exchange configuration requests and acknowledgements until they agree on the link parameters.

### Authentication

PPP can support authentication mechanisms such as PAP or CHAP.

Whether authentication is required depends on the cellular network and provider configuration.

The Simbase connection used during development does not require the application to provide PPP username/password credentials.

### IPCP

IPCP stands for:

```text
Internet Protocol Control Protocol
```

IPCP negotiates IPv4-related parameters.

This stage is particularly important because a successful serial connection alone does not provide usable Internet access.

The PPP session must obtain valid IP configuration.

## 11. IP Configuration

After successful PPP negotiation, lwIP has a PPP network interface with IP configuration.

Conceptually:

```text
SIM7670G / Cellular Network
            |
            | PPP negotiation
            v
        lwIP PPP
            |
            v
       IP interface
```

At this point, the ESP32-S3 can begin using normal IP networking.

Depending on the cellular network, the address assigned to the PPP interface may be a private carrier-side address rather than a publicly routable IPv4 address.

That does not by itself prevent outbound Internet access.

## 12. DNS

Applications generally need DNS in addition to basic IP connectivity.

For example:

```text
example.com
```

must be translated into an IP address before an HTTP client can connect to it.

PPP can obtain DNS information from the peer when peer DNS negotiation is enabled.

Conceptually:

```text
PPP connection
     |
     +---- IP configuration
     |
     +---- DNS configuration
```

Without working DNS, connections made directly to IP addresses may work while hostname-based connections fail.

For that reason, DNS problems should be distinguished from PPP link problems.

## 13. Default Network Interface

Once PPP is operational, the PPP interface can serve as the ESP32-S3's network path to the Internet.

The application then sees the networking stack approximately as:

```text
Application
    |
    v
Socket / HTTP / MQTT API
    |
    v
lwIP routing
    |
    v
PPP network interface
    |
    v
SIM7670G
```

This separation is one of the major benefits of PPP.

Higher-level networking code does not need to send modem-specific commands for every Internet operation.

## 14. Higher-Level Protocols

Once PPP provides a functioning IP interface, normal ESP-IDF networking functionality can operate over the cellular connection.

Examples include:

```text
HTTP
HTTPS
MQTT
MQTTS
DNS
SNTP
TCP sockets
UDP sockets
OTA
Cloud APIs
```

For example, an HTTP request follows approximately:

```text
esp_http_client
       |
       v
TCP / DNS
       |
       v
lwIP
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

The HTTP client does not need a special "cellular HTTP" implementation.

The same principle applies to MQTT and other socket-based protocols.

## 15. Separation of Responsibilities

The project architecture intentionally separates responsibilities.

### Application

Responsible for:

```text
HTTP
MQTT
OTA
Cloud communication
Business logic
```

### ESP-IDF / lwIP

Responsible for:

```text
TCP/IP
DNS
Sockets
PPP protocol
Routing
```

### PPP Connectivity Layer

Responsible for:

```text
UART initialization
Modem preparation
APN configuration
Dialing
AT mode → PPP mode transition
PPP UART TX
PPP UART RX
PPP connection lifecycle
```

### SIM7670G

Responsible for:

```text
Cellular radio
SIM access
Network registration
Packet-data service
Transport between PPP and cellular network
```

This separation keeps cellular-specific logic out of the higher-level application.

## 16. Why `CONNECT` Is a Critical Boundary

The most sensitive point in the connection sequence is:

```text
ATD*99#
    |
    v
CONNECT
```

Before `CONNECT`:

```text
UART = AT-command channel
```

After `CONNECT`:

```text
UART = PPP binary-data channel
```

Therefore, the software must coordinate ownership of UART carefully.

A simplified implementation model is:

```text
MODEM CONTROL
     |
     | AT commands
     v
   DIAL
     |
     | wait for CONNECT
     v
DATA MODE
     |
     | stop normal AT processing
     v
START PPP
     |
     v
PPP owns UART data path
```

Failing to maintain this boundary can result in symptoms such as:

```text
No CONNECT detected
PPP negotiation never starts
PPP frames interpreted as AT data
AT commands injected into PPP
Connection resets
Invalid PPP state
```

## 17. Connection State Model

The overall connection lifecycle can be viewed as a state machine:

```text
START
  |
  v
UART INITIALIZED
  |
  v
AT MODE
  |
  v
SIM READY
  |
  v
NETWORK REGISTERED
  |
  v
APN CONFIGURED
  |
  v
DIALING
  |
  v
CONNECT
  |
  v
PPP NEGOTIATING
  |
  v
PPP UP
  |
  v
IP CONNECTIVITY
  |
  v
APPLICATION NETWORKING
```

A failure at one stage should normally be diagnosed at that stage rather than immediately attributed to PPP as a whole.

For example:

```text
No response to AT
```

is fundamentally different from:

```text
AT works but ATD*99# never reaches data mode
```

which is different again from:

```text
CONNECT succeeds but IPCP never establishes IP
```

## 18. Connection Failure Layers

It is useful to think about troubleshooting in layers.

### Layer 1 — UART

```text
Can ESP32-S3 communicate with SIM7670G?
```

Test:

```text
AT
```

### Layer 2 — SIM

```text
Is the SIM available and ready?
```

Test:

```text
AT+CPIN?
```

### Layer 3 — Cellular Network

```text
Is the modem registered?
```

Inspect registration and signal information.

### Layer 4 — Packet Data / APN

```text
Is the correct PDP context configured?
```

Inspect:

```text
AT+CGDCONT?
```

### Layer 5 — Data Mode

```text
Does dialing enter the modem's data mode?
```

Transition:

```text
ATD*99#
    ↓
CONNECT
```

### Layer 6 — PPP

```text
Are PPP frames exchanged successfully?
```

Inspect LCP/IPCP negotiation.

### Layer 7 — IP

```text
Did the PPP interface obtain valid IP configuration?
```

### Layer 8 — DNS / Routing

```text
Can Internet destinations actually be reached?
```

### Layer 9 — Application Protocol

```text
Does HTTP, MQTT, TLS, or another application protocol work?
```

Separating these layers prevents an application-level failure from automatically being treated as a modem or PPP failure.

## 19. Why This Project Does Not Use `esp_modem`

This project communicates directly with the SIM7670G and lwIP PPP interfaces rather than placing `esp_modem` between the application and the modem.

The architecture is intentionally:

```text
Application
    |
ESP-IDF / lwIP
    |
PPP / PPPoS
    |
Project UART layer
    |
SIM7670G
```

rather than:

```text
Application
    |
esp_modem
    |
SIM7670G
```

This keeps the implementation relatively small and makes the important PPP-over-UART transition directly visible in the source code.

It also makes the project useful as a practical reference for understanding how PPPoS works underneath higher-level modem abstractions.

## 20. Waveshare-Specific Implementation

Although PPPoS itself is a general protocol mechanism, this repository is specifically intended for:

```text
Waveshare ESP32-S3-SIM7670G-4G
```

The tested hardware interface is:

```text
UART:       UART1
TX:         GPIO18
RX:         GPIO17
Baud:       115200
```

The tested Waveshare switch configuration is:

```text
CAM: ON
HUB: ON
4G:  ON
USB: ON
```

The combination of:

```text
Waveshare board hardware
        +
SIM7670G
        +
UART transport
        +
lwIP PPPoS
```

is the scope of this repository.

## 21. Complete Connection Sequence

Putting everything together, the normal connection sequence is:

```text
ESP32-S3 boots
      |
      v
ESP-IDF networking initialized
      |
      v
UART1 initialized
      |
      v
SIM7670G AT communication established
      |
      v
SIM checked
      |
      v
Cellular registration checked
      |
      v
APN / PDP context configured
      |
      v
ATD*99#
      |
      v
CONNECT
      |
      v
UART switches logically to PPP data mode
      |
      v
lwIP PPPoS starts
      |
      v
PPP TX frames → UART → SIM7670G
      |
      v
PPP RX frames ← UART ← SIM7670G
      |
      v
LCP negotiation
      |
      v
IPCP negotiation
      |
      v
IP / DNS configuration
      |
      v
PPP network interface operational
      |
      v
TCP/IP networking available
      |
      v
HTTP / MQTT / OTA / application traffic
```

The most important architectural boundary is:

```text
ATD*99# → CONNECT
```

Everything before that point is **modem control**.

Everything after that point, while the connection remains active, is primarily **PPP network traffic**.

## Next

For practical problems at any stage of this sequence, see:

```text
TROUBLESHOOTING.md
```

That document covers failures involving:

```text
UART communication
SIM readiness
Cellular registration
APN configuration
ATD*99#
CONNECT
PPP negotiation
IP assignment
DNS
Routing
Connection stability
```