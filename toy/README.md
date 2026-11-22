# LightDance ESP32 Firmware

Instructions for flashing firmware and monitoring ESP32 devices without installing ESP-IDF.

## Prerequisites

- [uv](https://docs.astral.sh/uv/) installed
- ESP32 connected via USB
- Prebuilt firmware (`merged.bin`)

## Flash Firmware

Flash the prebuilt `merged.bin` to your ESP32.

### macOS

```bash
uvx esptool --chip esp32 -p /dev/cu.usbserial-0001 \
  -b 460800 write-flash 0x0 merged.bin
```

### Linux

```bash
uvx esptool --chip esp32 -p /dev/ttyUSB0 \
  -b 460800 write-flash 0x0 merged.bin
```

### Windows

```powershell
uvx esptool --chip esp32 -p COM3 \
  -b 460800 write-flash 0x0 merged.bin
```

> **Note:** Replace the serial port with the actual port of your device. The `merged.bin` must be a complete flash image built for address `0x0`. Flashing a merged image may overwrite existing NVS data.

## Serial Monitor

Monitor ESP32 logs and interact with the device through UART.

### macOS

```bash
uvx --from esp-idf-monitor idf-monitor \
  -p /dev/cu.usbserial-0001 -b 115200
```

### Linux

```bash
uvx --from esp-idf-monitor idf-monitor \
  -p /dev/ttyUSB0 -b 115200
```

### Windows

```powershell
uvx --from esp-idf-monitor idf-monitor \
  -p COM3 -b 115200
```

Press `Ctrl + ]` to exit the monitor.

## Find Your Serial Port

### macOS

```bash
ls /dev/cu.*
```

### Linux

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

### Windows

Open **Device Manager → Ports (COM & LPT)** to find the device's COM port.

## Troubleshooting

**Device not found**

- Check the USB connection and cable.
- Verify the serial port.
- Ensure the required USB-to-UART driver is installed.

**Permission denied (Linux)**

Add your user to the `dialout` group:

```bash
sudo usermod -aG dialout $USER
```

Log out and log back in for the change to take effect.

**Failed to connect**

- Close any other program using the serial port.
- Try holding the BOOT button while starting the flash process.
- Try reducing the flashing baud rate to `115200`.

## Notes

- ESP-IDF installation is **not required**.
- `uvx` automatically downloads and runs the required Python tools.
- Flashing and monitoring use different baud rates by default: `460800` and `115200`, respectively.
