| Supported Targets | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | -------- | -------- | -------- | -------- |

# Espressif's Additions to TinyUSB - Runtime Configuration Test Application

This directory contains Unity tests that validate Espressif-specific integration of TinyUSB.

The tests focus on:

- TinyUSB configuration helpers (default macros, per-port config).
- USB Device descriptors (FS/HS, string descriptors, edge cases).
- USB peripheral / PHY configuration for full-speed and high-speed.
- TinyUSB task configuration (CPU pinning, invalid parameters).
- Multitask access to the TinyUSB driver (concurrent installs).

The test prints a numbered menu, for example:

```
(1) "Config: Default macros arguments" [runtime_config][default]
(2) "Config: Full-speed (High-speed)" [runtime_config][full_speed]
...
```

You can run all tests by running `pytest` or select individual ones by name and number.

## Tags

Each test is tagged with categories and modes:

### Categories

- [runtime_config] – Tests focusing on `tinyusb_config_t` and runtime configuration.
- [periph] – Tests that directly exercise the USB peripheral (USB OTG 1.1 or USB OTG 2.0).
- [task] – Tests related to the dedicated TinyUSB task configuration.

### Speed / Mode

- [default] – Generic, target-agnostic.
- [full_speed] – Tests specific to USB OTG 1.1 / Full-speed port.
- [high_speed] – Tests specific to USB OTG 2.0 / High-speed port.

These tags can be used by test runners / CI to select or filter tests.
