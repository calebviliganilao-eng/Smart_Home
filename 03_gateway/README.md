# SMART_GATEWAY — complete PlatformIO project

In VS Code, open the `03_gateway` folder containing `platformio.ini`. The lab folder can contain spaces.

The default target is the ESP32-S3 N16R8. Connect its optional status LED through a 220–330 Ω resistor to GPIO18, with the cathode connected to GND. GPIO35 is reserved for octal PSRAM. The original ESP32 target remains available with `-e esp32dev` and uses GPIO19 for the status LED.

To use your current Wi-Fi settings, copy `src/wifi_secrets.h` from your previous gateway project if it exists. The gateway can also reuse credentials saved on the board. To configure a different 2.4 GHz network, copy `src/wifi_secrets.example.h` to `src/wifi_secrets.h` and edit the copy.

Close Serial Monitor and connect ESP32 #3. Check its port with `pio device list`. Then run in the `03_gateway` terminal:

```powershell
pio run -e esp32s3_n16r8 -t upload --upload-port COM5
pio device monitor -p COM5 -b 115200
```

Use the actual gateway COM port. This is a standard upload using the prior partition layout, so the Wi-Fi and BLE Mesh settings can be reused.

1. From a device on the same local Wi-Fi, open the current `http://` address in the gateway's serial output and refresh the page. Open Gateway settings, enter SMART_SWITCH's address `0004`, and select Save.
2. If the page asks for an AppKey, open the existing mesh network in nRF Mesh and use Connect if needed. Install the shared AppKey on SMART_GATEWAY, then bind it to the Generic OnOff Client (0x1001). Bind the same AppKey to the SMART_SWITCH Generic OnOff Server (0x1000).
3. If the page asks for gateway provisioning, provision SMART_GATEWAY into the network containing SMART_SWITCH. If the gateway's own address is 0004, check its current network and address assignment before sending a command.
4. Keep SMART_SWITCH and SMART_GATEWAY powered. Temporarily unplug the sensor while testing manual Turn ON and Turn OFF. The dashboard reads the relay state from the switch and displays acknowledgments for manual commands.

If the webpage itself cannot open, first look for the gateway's current IP in its Serial Monitor. If the IP is present but unreachable from your PC, test access with `Test-NetConnection <gateway-IP> -Port 80` after replacing the placeholder with the printed IP. If no IP is printed, check the gateway’s 2.4 GHz Wi-Fi credentials in `src/wifi_secrets.h`.

## Checks

Run these in the gateway folder. The four Unity tests run on the PC with a native C compiler.

```powershell
pio test
pio check
```

See [PlatformIO testing](../docs/PLATFORMIO_TESTING.md) for optional tests on the ESP32-S3.
