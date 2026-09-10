# Wiring

This document describes the tested wiring for the **bmax_sys Raspberry Pi Pico + W5500 MAVLink Bridge**.

## Raspberry Pi Pico ↔ W5500

| Raspberry Pi Pico | W5500 | Function |
|---|---|---|
| 3V3 | V | Power |
| GND | G | Ground |
| GP16 | MI | MISO |
| GP17 | CS | Chip Select |
| GP18 | SCK | SPI Clock |
| GP19 | MO | MOSI |
| GP20 | RST | Reset |

## Raspberry Pi Pico ↔ Flight Controller

| Raspberry Pi Pico | Flight Controller | Function |
|---|---|---|
| VSYS | 5V | Power |
| GND | GND | Ground |
| GP0 (TX) | RX | UART TX |
| GP1 (RX) | TX | UART RX |

## UART Connection

UART lines must be crossed:

Pico GP0 TX → Flight Controller RX

Pico GP1 RX ← Flight Controller TX

## Data Path

Flight Controller  
↕ UART  
Raspberry Pi Pico  
↕ SPI  
W5500  
↕ Ethernet / UDP  
QGroundControl

## Default Network Configuration

| Parameter | Default |
|---|---|
| Device IP | `192.168.88.50` |
| Target IP | `192.168.88.11` |
| Subnet Mask | `255.255.255.0` |
| Gateway | `192.168.88.1` |
| UDP Port | `14550` |
| UART Baudrate | `115200` |

Web configuration interface:

`http://192.168.88.50`

## Important

The wiring above corresponds to the hardware configuration used during real testing of this project.

Verify the pinout and voltage requirements of your specific W5500 module before connecting power.

Do not connect TX directly to TX or RX directly to RX.

---

**bmax_sys**  
Embedded • Networking • Robotics
