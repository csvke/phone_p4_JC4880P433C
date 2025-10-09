# WiFi SDIO Troubleshooting - JC4880P443C

## Current Status

**Date:** October 9, 2025

### ✅ What's Working:
- ESP32-P4 (Host) running ESP-Hosted v2.5.10
- ESP32-C6 (Slave) running ESP-Hosted v2.5.10 (CONFIRMED via UART)
- Both firmwares compiled and flashed successfully
- SDIO pins configured correctly on P4 side:
  - CMD: GPIO19
  - CLK: GPIO18
  - D0-D3: GPIO14-17
  - RESET: GPIO54
- P4 configured to reset C6 on every boot

### ❌ Problem:
**SDIO handshake not happening** - P4 and C6 are not establishing communication

**Expected messages (NOT appearing):**
```
I (xxx) transport: Received INIT event from ESP32 peripheral
I (xxx) transport: Base transport is set-up
I (xxx) sdio_wrapper: SDIO master: Data-Lines: 4-bit Freq(KHz)[40000 KHz]
```

**Actual P4 boot log:**
```
I (1374) host_init: ESP Hosted : Host chip_ip[18]
I (1379) H_API: ESP-Hosted starting. Hosted_Tasks: prio:23, stack: 5120
I (1387) H_API: ** add_esp_wifi_remote_channels **
I (1391) transport: Add ESP-Hosted channel IF[1]: S[0] Tx[...] Rx[...]
I (1399) transport: Add ESP-Hosted channel IF[2]: S[0] Tx[...] Rx[...]
I (1415) H_SDIO_DRV: sdio_data_to_rx_buf_task started
[... no further SDIO messages ...]
```

**Actual C6 boot log:**
```
I (438) co-pro-main: ESP-Hosted-MCU Slave FW version :: 2.5.10
I (445) co-pro-main: Transport used :: SDIO only
I (460) SDIO_SLAVE: Using SDIO interface
I (464) SDIO_SLAVE: sdio_init: sending mode: SDIO_SLAVE_SEND_PACKET
I (470) SDIO_SLAVE: sdio_init: ESP32 SDIO DriverTxQ[20] timing[0]
I (476) co-pro-main: Mandate host wakeup
I (479) co-pro-main: host reset handler task started
[... C6 waits silently for SDIO commands ...]
```

## Possible Root Causes

### 1. **Hardware Connection Issue**
The SDIO physical connections might not be reliable:
- Loose solder joints on SDIO pins
- PCB trace issues
- Incorrect voltage levels (3.3V required)
- Missing pull-up resistors on CMD/DATA lines

**Test:** Use oscilloscope/logic analyzer to verify SDIO signal integrity

### 2. **Timing Configuration Mismatch**
C6 slave shows `timing[0]` but P4 host might expect different timing:
- SDIO clock frequency mismatch
- Sample timing differences
- Bus width negotiation failure

**Current slave config:**
- Mode: `SDIO_SLAVE_SEND_PACKET`
- Timing: `[0]`
- TxQ: `[20]`

### 3. **Reset Sequencing Problem**
Even though GPIO54 is configured, the reset might not be working:
- Reset pulse too short
- C6 not in correct state after reset
- Race condition between P4 init and C6 boot

**Test:** Add delays in P4 SDIO initialization

### 4. **SDIO Slave Not Ready**
C6 might be booting but SDIO peripheral not initializing properly:
- SDIO pins not configured on C6 side
- SDIO peripheral power issue
- Slave firmware configuration error

### 5. **ESP-IDF Version Incompatibility**
Both are using ESP-IDF v5.5.1, but there might be subtle differences in:
- SDIO driver implementation
- esp_hosted component expectations
- Peripheral initialization order

## Troubleshooting Steps

### Step 1: Enable Maximum Debug Logging on P4

Edit `sdkconfig` or use menuconfig:
```bash
cd /Users/frankieyuen/JC4880P443C/phone_p4_JC4880P433C
idf.py menuconfig
```

Navigate to:
- **Component config** → **Log output** → **Default log verbosity** → Set to **Debug**
- **Component config** → **ESP-Hosted** → Enable verbose logging (if available)

Add to `sdkconfig.defaults`:
```ini
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_ESP_HOSTED_DEBUG_ENABLED=y
```

Rebuild and check for hidden error messages.

### Step 2: Enable Debug Logging on C6 Slave

```bash
cd /Users/frankieyuen/JC4880P443C/phone_p4_JC4880P433C
idf.py -C managed_components/espressif__esp_hosted/slave/ -B build_slave menuconfig
```

Set log level to Debug, rebuild and reflash.

### Step 3: Verify SDIO Pin Connections Physically

Use a multimeter to verify continuity:
- P4 GPIO19 (CMD) → C6 SDIO_CMD pin
- P4 GPIO18 (CLK) → C6 SDIO_CLK pin
- P4 GPIO14-17 (D0-D3) → C6 SDIO_D0-D3 pins
- P4 GPIO54 → C6_CHIP_PU (reset)
- GND → GND (common ground)

### Step 4: Check Schematic for Additional Requirements

Review the JC4880P443C schematic for:
- External pull-up resistors on SDIO lines (typically 10kΩ-47kΩ)
- Level shifters (should NOT be present - both chips are 3.3V)
- Enable pins or power switches for C6
- SDIO voltage supply (should be 3.3V)

### Step 5: Test with Manufacturer's Working Example

The manufacturer provides working examples. Compare configuration:
```bash
cd /Users/frankieyuen/JC4880P443C/JC4880P443C_I_W_esp_brookesia_phone_esp-idf_v5.5.1
# Check their WiFi configuration
grep -r "SDIO\|ESP_HOSTED" . | head -20
```

Look for differences in:
- SDIO pin assignments
- esp_hosted component version
- menuconfig settings
- Initialization sequence

### Step 6: Try Alternative SDIO Configuration

The C6 slave is using `SDIO_SLAVE_SEND_PACKET` mode. Try changing to streaming mode:

In slave menuconfig:
```
Component config → ESP-Hosted Config → SDIO Configuration → 
  [ ] Enable SDIO Streaming Mode
```

Or try the opposite if it's currently disabled.

### Step 7: Manual Reset Test

Add manual delay in P4 code after SDIO init:
```c
// In transport initialization
esp_hosted_reset_slave();  // Existing reset
vTaskDelay(pdMS_TO_TICKS(1000));  // Add 1 second delay
// Continue with SDIO init
```

This ensures C6 has time to fully boot before P4 tries to communicate.

### Step 8: Check for Known Issues

Search ESP-Hosted GitHub issues:
```
site:github.com/espressif/esp-hosted-mcu "ESP32-P4" "ESP32-C6" SDIO "not connecting"
```

Look for similar reported problems and solutions.

## Next Steps if Still Not Working

1. **Contact Board Manufacturer**
   - Ask for known WiFi configuration issues
   - Request confirmed-working firmware binaries
   - Check if specific hardware revision is needed

2. **Try UART Transport Instead of SDIO**
   - ESP-Hosted supports UART as alternative
   - Slower but more reliable for debugging
   - Can verify if WiFi stack itself works

3. **Use Different ESP-Hosted Version**
   - Try older stable version (e.g., v2.4.x)
   - Version 2.5.10 might have regression
   - Check esp_hosted CHANGELOG for P4+C6 notes

4. **Hardware Modification**
   - Add external pull-up resistors if missing
   - Check/replace C6 chip if damaged
   - Verify power supply quality (stable 3.3V)

## Reference Documentation

- ESP-Hosted GitHub: https://github.com/espressif/esp-hosted-mcu
- ESP32-P4 SDIO Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/sdio_slave.html
- ESP32-C6 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf
- Board schematic: `/Users/frankieyuen/JC4880P443C/Manufacturer Docs/JC4880P443C_I_W/5-Schematic/`

## Version Information

- **ESP-IDF:** v5.5.1
- **ESP-Hosted (P4 Host):** v2.5.10
- **ESP-Hosted (C6 Slave):** v2.5.10 (confirmed)
- **Board:** JC4880P443C with ESP32-P4 + ESP32-C6
- **Compile Date (P4):** Oct 9 2025 00:57:34
- **Compile Date (C6):** Oct 9 2025 12:12:52

## Logs Captured

- P4 boot log: `/tmp/p4_wifi_boot.log`
- C6 boot log: Shown in monitor output above
- Build logs: `build/log/` and `build_slave/log/`
