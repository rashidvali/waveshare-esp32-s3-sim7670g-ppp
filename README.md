# ESP32-S3 + SIM7670 PPP-over-UART Project

This project demonstrates a working PPP-over-UART connection between ESP32-S3 and SIM7670,
with correct LCP/IPCP negotiation, IPv4 assignment, and full Internet connectivity.

## Features
- SIM7670 UART AT initialization
- ATD*99# dialing into PPP data mode
- lwIP PPPoS stack configuration
- Real IP address acquisition
- Successful outbound TCP connection (verified via connect() to 1.1.1.1:80)
- Automatic re-dial on disconnect
- Extensive debug logging for LCP/IPCP/VJ
- Clean modular structure for future integration with MQTT/HTTP

## Hardware
- ESP32-S3 SIM7670G 4G Development Board (Waveshare) (UART mode)
- SIM card with data enabled (APN: simbase)

## Status
PPP Internet connectivity is fully operational.
