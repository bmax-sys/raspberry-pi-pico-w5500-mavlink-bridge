# Testing

The Raspberry Pi Pico + W5500 MAVLink Bridge was tested with real hardware.

## Hardware

- Raspberry Pi Pico (RP2040)
- W5500 Ethernet module
- ArduPilot flight controller
- Ethernet network
- QGroundControl

## Ethernet Test

The W5500 was successfully detected by the Raspberry Pi Pico.

Test results:

- W5500 detected
- Ethernet link: ONLINE
- Static IP: `192.168.88.50`
- Ping to the device: successful

## Web Interface Test

The built-in configuration interface was successfully opened at:

`http://192.168.88.50`

The interface displays network and UART configuration parameters.

![Web Interface](images/web-interface.jpg)

## MAVLink Telemetry Test

The flight controller was connected to the Raspberry Pi Pico using UART0:

- GP0 TX → Flight Controller RX
- GP1 RX ← Flight Controller TX
- UART baudrate: `115200`

MAVLink telemetry from the flight controller was successfully received by QGroundControl over Ethernet/UDP.

Data path:

`Flight Controller → UART → Raspberry Pi Pico → W5500 → Ethernet → UDP → QGroundControl`

UDP port:

`14550`

## Wiring Diagram

![Wiring Diagram](images/wiring-diagram.png)

## Result

The following functions were successfully verified:

- Ethernet communication
- Static IP configuration
- Device ping
- Built-in web interface
- UART communication with the flight controller
- MAVLink telemetry over UDP
- QGroundControl vehicle detection and telemetry reception

The bridge operated correctly with the tested hardware configuration.

---

**bmax_sys**  
Embedded • Networking • Robotics
