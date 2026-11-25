# PlatformIO testing for Lab 2

Open the sensor, switch, or gateway folder in PowerShell. Each folder has its own PlatformIO project. Use PlatformIO and a native C compiler on your PATH. On this Windows setup, GCC comes from MSYS2 UCRT64.

```powershell
pio test
pio check
pio run
```

`pio test` runs the Unity suite on the PC. It works with every ESP unplugged. `pio check` analyzes the firmware source and the PC tests. `pio run` builds the firmware and PC test executable.

| Project | PC tests | What they exercise |
| --- | ---: | --- |
| 01_sensor | 4 | Flashlight and cover readings map to bright/OFF and dark/ON |
| 02_switch | 2 | The relay's active-high output level for ON and OFF |
| 03_gateway | 4 | Living Room, removal of the three room tabs, the Smart light controls and status endpoint in the embedded page |

Physical sensor readings, relay movement, Wi-Fi, and Bluetooth Mesh delivery require the lab boards.

## Optional tests on an ESP

These commands install test firmware on the selected board. Run only the command for the project you opened. Close Serial Monitor first and use the actual port from `pio device list`. For the relay, power down and disconnect the load before flashing.

| Folder | Run on the board | Restore its normal application |
| --- | --- | --- |
| 01_sensor | `pio test -e esp32dev -f test_sensor_logic --upload-port COM3 --test-port COM3` | `pio run -e esp32dev -t upload --upload-port COM3` |
| 02_switch | `pio test -e esp32dev -f test_switch_pins --upload-port COM3 --test-port COM3` | `pio run -e esp32dev -t upload --upload-port COM3` |
| 03_gateway | `pio test -e esp32s3_n16r8 -f test_gateway_page --upload-port COM5 --test-port COM5 --no-reset --monitor-dtr 0 --monitor-rts 0` | `pio run -e esp32s3_n16r8 -t upload --upload-port COM5` |

The switch also checks GPIO4 and GPIO19 output capability on the ESP. If PlatformIO waits for output after printing Testing, press EN once.
