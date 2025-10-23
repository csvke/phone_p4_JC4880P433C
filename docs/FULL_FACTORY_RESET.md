# Full Factory Reset Guide

This guide describes how to flash the manufacturer's original firmware to both the ESP32-P4 and ESP32-C6 chips on the JC4880P443 display module, effectively performing a complete factory reset.

## Overview

The JC4880P443 module contains two ESP32 chips that must be flashed with matching firmware versions:
- **ESP32-P4**: Main application processor (camera, display, UI)
- **ESP32-C6**: WiFi/Bluetooth slave processor (ESP-Hosted protocol)

⚠️ **Important**: Both chips must run compatible ESP-Hosted protocol versions. Flashing only the P4 firmware will cause crashes due to protocol version mismatches.

## Prerequisites

### Hardware Requirements
- JC4880P443 display module
- USB cable for ESP32-P4 (built-in USB connection)
- USB-to-Serial adapter for ESP32-C6 (CH343G or similar)
- Jumper wire or connection to GND for C6 download mode

### Software Requirements
- `esptool.py` (installed via ESP-IDF)
- Manufacturer firmware binaries:
  - `JC4880P443_P4-V1.3.bin` (13.7 MB) - ESP32-P4 firmware
  - `JC4880P443_C6.bin` (1.15 MB) - ESP32-C6 firmware

### Firmware Location
Manufacturer firmware binaries are typically located at:
```
/Users/frankieyuen/JC4880P443C/Manufacturer Docs/C3986-1 JC4880P443C_I_W.rar/JC4880P443C_I_W/1-Demo/
```

Extract the `.rar` archive to access the binary files.

## Step 1: Flash ESP32-P4 Firmware

### 1.1 Connect ESP32-P4
1. Connect the JC4880P443 module to your computer via USB
2. The P4 chip should appear as `/dev/cu.usbmodem1101` (or similar)
3. Verify connection:
   ```bash
   ls /dev/cu.usbmodem*
   ```

### 1.2 Flash P4 Firmware
Navigate to the firmware directory and flash:

```bash
cd "/Users/frankieyuen/JC4880P443C/Manufacturer Docs/C3986-1 JC4880P443C_I_W.rar/JC4880P443C_I_W/1-Demo/"

esptool.py --chip esp32p4 write_flash 0x0 "JC4880P443_P4-V1.3.bin"
```

### 1.3 Expected Output
```
esptool.py v4.8.1
Serial port /dev/cu.usbmodem1101
Connecting...
Chip is ESP32-P4 (revision v1.0)
Features: High-Performance MCU
Crystal is 40MHz
MAC: 30:ed:a0:e1:9e:bb
...
Wrote 13697024 bytes (7174364 compressed) at 0x00000000 in 113.2 seconds
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

**Note**: After flashing only the P4, the device may crash with `lwip/netif` errors due to ESP-Hosted protocol version mismatch with the C6. This is expected and will be resolved in Step 2.

## Step 2: Flash ESP32-C6 Firmware

### 2.1 Locate C6 Serial Pins
The ESP32-C6 requires external serial connection via the Expand IO connector:

| Pin | Function | Description |
|-----|----------|-------------|
| 20  | C6_TXD0  | Serial TX (connect to adapter RX) |
| 22  | C6_RXD0  | Serial RX (connect to adapter TX) |
| 24  | C6_IO9   | Boot mode control (pull to GND for download mode) |
| 2   | GND      | Ground reference |

### 2.2 Wire USB-to-Serial Adapter
Connect your USB-to-Serial adapter (CH343G or similar):

```
USB Adapter    →    JC4880P443 Expand IO
-----------         --------------------
TX         →        Pin 22 (C6_RXD0)
RX         →        Pin 20 (C6_TXD0)
GND        →        Pin 2  (GND)
```

### 2.3 Enter Download Mode
1. **Connect C6_IO9 (Pin 24) to GND (Pin 2)** using a jumper wire
2. Power cycle the board or press reset
3. The C6 will enter download mode

### 2.4 Verify C6 Connection
Check that the USB-to-Serial adapter appears:
```bash
ls /dev/tty.wchusbserial*
# Example: /dev/tty.wchusbserial575C0150151
```

### 2.5 Flash C6 Firmware
Flash the C6 firmware (keep IO9 connected to GND):

```bash
cd "/Users/frankieyuen/JC4880P443C/Manufacturer Docs/C3986-1 JC4880P443C_I_W.rar/JC4880P443C_I_W/1-Demo/"

esptool.py --chip esp32c6 --port /dev/tty.wchusbserial575C0150151 --baud 460800 write_flash 0x0 "JC4880P443_C6.bin"
```

**Note**: Replace `/dev/tty.wchusbserial575C0150151` with your actual serial port.

### 2.6 Expected Output
```
esptool.py v4.8.1
Serial port /dev/tty.wchusbserial575C0150151
Connecting...
Chip is ESP32-C6FH4 (QFN32) (revision v0.2)
Features: WiFi 6, BT 5, IEEE802.15.4
Crystal is 40MHz
MAC: 9c:13:9e:c0:85:38
...
Wrote 1146768 bytes (628738 compressed) at 0x00000000 in 14.5 seconds
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

## Step 3: Exit Download Mode and Test

### 3.1 Exit Download Mode
1. **Disconnect C6_IO9 from GND** (remove jumper wire from Pin 24)
2. This allows the C6 to boot normally

### 3.2 Power Cycle
1. Disconnect and reconnect USB power to the JC4880P443 module
2. Or press the reset button if available

### 3.3 Verify Boot
Monitor the P4 serial output:
```bash
idf.py -p /dev/cu.usbmodem1101 monitor
```

You should see:
- ESP-Hosted control path initialization
- WiFi/network stack initialization
- UI and camera system startup
- No `lwip/netif` errors

### 3.4 Test Camera Application
1. Navigate to the camera app from the main menu
2. Verify 30 FPS preview is working
3. Check UI responsiveness and stability

## Troubleshooting

### P4 Crashes with "control path not initialized"
**Symptom**: After flashing P4, device crashes with:
```
E (4572) transport: control path not initialized
```

**Cause**: ESP-Hosted protocol version mismatch between P4 and C6

**Solution**: Flash the matching C6 firmware (Step 2)

### Cannot Connect to C6
**Symptom**: `esptool.py` cannot detect C6 chip

**Possible Causes**:
1. C6_IO9 not connected to GND - device is not in download mode
2. Wrong serial port selected
3. Incorrect TX/RX wiring (swap TX and RX)
4. USB-to-Serial adapter driver issues

**Solutions**:
- Double-check Pin 24 (C6_IO9) is connected to GND
- Verify serial port: `ls /dev/tty.*`
- Try swapping TX and RX connections
- Test adapter with another device

### Flash Verification Failed
**Symptom**: Hash verification fails during flashing

**Cause**: Corrupt binary or connection issues

**Solution**:
- Re-extract firmware from `.rar` archive
- Try lower baud rate: `--baud 115200`
- Check USB cable and connections

### Device Still Not Working After Both Flashes
**Checklist**:
1. ✅ Both P4 and C6 firmware flashed successfully?
2. ✅ C6_IO9 disconnected from GND after flashing?
3. ✅ Board power cycled after C6 flash?
4. ✅ No hardware damage or loose connections?

If all checks pass and device still fails, try:
- Re-flash both chips again
- Check manufacturer documentation for updates
- Verify firmware binaries are correct version

## Firmware Information

### ESP32-P4 Firmware (JC4880P443_P4-V1.3.bin)
- **Version**: V1.3
- **Size**: 13,697,024 bytes (13.7 MB)
- **Compressed**: 7,174,364 bytes
- **Flash Time**: ~113 seconds @ default baud
- **ESP-IDF Version**: v5.4.1
- **Features**: Camera app, UI, display driver, ESP-Hosted host

### ESP32-C6 Firmware (JC4880P443_C6.bin)
- **Version**: Matching P4 V1.3
- **Size**: 1,146,768 bytes (1.15 MB)
- **Compressed**: 628,738 bytes
- **Flash Time**: ~15 seconds @ 460800 baud
- **ESP-IDF Version**: v5.4.1
- **Features**: WiFi 6, Bluetooth 5, IEEE802.15.4, ESP-Hosted slave

## Returning to Custom Firmware

After testing the manufacturer firmware, you can flash your custom/refactored firmware:

### Flash Custom P4 Firmware
```bash
cd /Users/frankieyuen/JC4880P443C/phone_p4_JC4880P433C
idf.py flash monitor
```

### Flash Custom C6 Firmware (if using custom ESP-Hosted)
```bash
cd /Users/frankieyuen/JC4880P443C/esp-hosted-mcu/slave
idf.py set-target esp32c6
idf.py flash monitor
```

**Note**: Remember to enter C6 download mode (connect IO9 to GND) before flashing custom C6 firmware.

## Reference

- **P4 Serial Port**: `/dev/cu.usbmodem1101` (built-in USB)
- **C6 Serial Port**: `/dev/tty.wchusbserial*` (external adapter)
- **P4 Chip**: ESP32-P4 revision v1.0
- **C6 Chip**: ESP32-C6FH4 (QFN32) revision v0.2
- **ESP-Hosted Protocol**: SDIO-based communication between P4 (host) and C6 (slave)

## Additional Resources

- [ESP32-P4 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf)
- [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- [ESP-Hosted Documentation](https://github.com/espressif/esp-hosted)
- [esptool.py Documentation](https://docs.espressif.com/projects/esptool/en/latest/)

---

**Last Updated**: October 23, 2025  
**Firmware Version**: JC4880P443 P4-V1.3 / C6 (matching)  
**Tested On**: macOS with ESP-IDF v5.5.1
