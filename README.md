# Lab 2 — ESP32 Smart Home

Each node has its own PlatformIO project with ESP-IDF 5.1.1. The sensor and switch target the ESP32 DevKit. The gateway defaults to the ESP32-S3 N16R8.

| Open in VS Code | Hardware and behavior |
| --- | --- |
| `01_sensor` | Digital light sensor DO on GPIO27. DO=0 is bright/OFF and DO=1 is covered/ON. |
| `02_switch` | Single relay IN on GPIO4. Active-HIGH by default. |
| `03_gateway` | 2.4 GHz Wi-Fi dashboard with a Smart light toggle, explicit ON/OFF controls, and a saved switch address. |

See [the circuit diagram](docs/ESP32_Smart_Home_Circuit.png) for the original ESP32 wiring and relay power requirements. Use the relay indicator or click for the first test, with the relay's load terminals left open. Each ESP32 uses USB power. The optional status LED uses GPIO19 on the original ESP32 and GPIO18 on the ESP32-S3 gateway. Connect it through a 220–330 Ω resistor with the cathode returning to GND.

## Build and upload

1. Open each node folder separately in VS Code with PlatformIO. The lab folder can contain spaces. PlatformIO writes build files to `build` under its core directory (usually `%USERPROFILE%\.platformio` on Windows).
2. On the gateway, copy `03_gateway/src/wifi_secrets.example.h` to `03_gateway/src/wifi_secrets.h` and enter your 2.4 GHz Wi-Fi credentials if the board needs a network configured. The firmware can also use the credentials already saved in ESP Wi-Fi storage.
3. Use `pio device list` to find the connected board’s COM port. In that node's terminal, use the following commands. Replace COM3 with the actual port and close the monitor with Ctrl+C before an upload.

   ```powershell
   pio run
   pio run -e esp32dev -t upload --upload-port COM3
   pio device monitor -p COM3 -b 115200
   ```

   For the S3 gateway, replace `-e esp32dev` with `-e esp32s3_n16r8`. Use a normal upload with the existing partition layout to preserve BLE Mesh credentials in NVS. If a board is already provisioned, select its existing entry in nRF Mesh.

## Connect all three nodes in nRF Mesh

1. Open the same mesh network for all three boards. Power the boards, then provision each new node using **Network → Add Node**. Choose **No OOB** if prompted. Open the existing node entry for a board that is already provisioned.
2. If the app loses its proxy connection after provisioning, return to **Network → Connect** and select an available proxy in the same network.
3. Install the same network Application Key on all three nodes from their **Application Keys** screens. Under each node's first element, bind that key to the models below.
4. Create a group named **SmartHome** and use the actual assigned group address for both the publication and subscription. For example, the address can be `0xC123`.

   | Node | Model | Action |
   | --- | --- | --- |
   | SMART_SENSOR | Generic OnOff Server (0x1000) | Bind AppKey and set publication to SmartHome using that AppKey. Set publication period to 0. |
   | SMART_SWITCH | Generic OnOff Client (0x1001) | Bind the same AppKey and subscribe to SmartHome. |
   | SMART_SWITCH | Generic OnOff Server (0x1000) | Bind the same AppKey for manual ON/OFF commands. |
   | SMART_GATEWAY | Generic OnOff Client (0x1001) | Bind the same AppKey for manual ON/OFF commands. |

5. Find SMART_SWITCH’s primary unicast address in node details or its startup log. Open the gateway IP printed in its serial monitor from a browser on the same LAN. Open **Gateway settings**, enter SMART_SWITCH’s primary unicast address, and select **Save**. Use the switch’s own address, which must be distinct from the gateway’s address.
6. Check the four lab outputs. Gateway ON energizes the relay, gateway OFF releases it, covering the sensor energizes it, and shining light on the sensor releases it. The dashboard reads the switch's relay state while the page is open and reports manual-command acknowledgments separately.

If the raw DO readings differ from the table, set `DARK_WHEN_DO_LOW` in `01_sensor/src/sensor_logic.h` to match the physical module and update the expected readings in its unit test. Adjust the sensor’s hardware threshold if DO stays constant. The module gives a digital threshold reading in place of the original lab’s lux measurement.

## PlatformIO checks

Run these in each node folder with the ESP unplugged:

```powershell
pio test
pio check
```

The sensor runs four Unity tests, the switch runs two, and the gateway runs four. See [PlatformIO testing](docs/PLATFORMIO_TESTING.md) for compiler setup, coverage, and optional board tests.
