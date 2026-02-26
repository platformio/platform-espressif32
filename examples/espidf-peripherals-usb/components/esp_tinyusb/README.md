# Espressif's Additions to TinyUSB

[![Component Registry](https://components.espressif.com/components/espressif/esp_tinyusb/badge.svg)](https://components.espressif.com/components/espressif/esp_tinyusb)

This component extends TinyUSB with features that simplify integration into ESP-IDF applications.

It provides both default and customizable configurations for TinyUSB, enabling USB device functionality on ESP chips with USB-OTG support.

### Run-time configuration

During configuration, the following parameters can be set when installing the driver:

- Descriptors
- Peripheral port
- Task parameters (size, priority, and CPU affinity)
- USB PHY parameters

### Default configuration

Run-time default configuration for Device Stack is managed internally via the `TINYUSB_DEFAULT_CONFIG()` macros.

### Custom configuration

Manual configuration for Device Stack: descriptors, peripheral port, task, and USB PHY parameters can be set as needed.

### Build-Time configuration

Configure the Device Stack using `menuconfig`:

- TinyUSB log verbosity
- Default device/string descriptor used by the default configuration macros
- Class-specific options (CDC-ACM, MSC, MIDI, HID, DFU, BTH, ECM/NCM/RNDIS, Vendor etc.)

### Supported classes

- USB Serial Device (CDC-ACM) with optional Virtual File System support.
- Input and output streams through the USB Serial Device (available only when Virtual File System support is enabled).
- Mass Storage Device Class (MSC): create USB flash drives using SPI Flash or SD/MMC as storage media.
- Support for other USB classes (MIDI, HID, etc.) directly via TinyUSB.

## How to use?

This component is distributed via [IDF component manager](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-component-manager.html). Just add `idf_component.yml` file to your main component with the following content:

```yaml
## IDF Component Manager Manifest File
dependencies:
  esp_tinyusb: "~2.0.0"
```

Or simply run:

```
idf.py add-dependency esp_tinyusb~2.0.0
```

## Breaking changes migration guides

- [v2.0.0](../../docs/device/migration-guides/v2/)

## USB Device Stack: usage, installation & configuration

Hardware-related documentation can be found in the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/peripherals/usb_device.html).

### Structure overview

The Device Stack is built on top of TinyUSB and provides:

- Custom USB descriptor support
- Serial device (CDC-ACM) support
- Standard stream redirection through the serial device
- Storage media support (SPI Flash and SD Card) for the USB MSC class
- A dedicated task for TinyUSB servicing

### Installation

Install the Device Stack by calling `tinyusb_driver_install` with a `tinyusb_config_t` structure.

A default configuration is available using the `TINYUSB_DEFAULT_CONFIG()` macro.

The default installation automatically configures the port (High-speed if supported by the hardware, otherwise Full-speed), task (with default parameters), USB PHY, Device Event callback and descriptors.

Default descriptors are provided for the following USB classes: CDC, MSC, and NCM.

> **⚠️ Important:** For demonstration purposes, all error handling logic has been removed from the code examples. Do not ignore proper error handling in actual development.

```c
  #include "tinyusb_default_config.h"

  void main(void) {
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tinyusb_driver_install(&tusb_cfg);
  }
```

### Device Event callback

USB Device Event callback allows to get the following events during USB Device lifecycle:

- `TINYUSB_EVENT_ATTACHED`: Device attached to the USB Host
- `TINYUSB_EVENT_DETACHED`: Device detached from the USB Host
- `TINYUSB_EVENT_SUSPENDED`: Device entered suspended state
- `TINYUSB_EVENT_RESUMED`: Device was resumed from suspended state

To configure the USB Device Event Callback, provide the callback to the `TINYUSB_DEFAULT_CONFIG()` macros:

```c
  #include "tinyusb_default_config.h"

  static void device_event_handler(tinyusb_event_t *event, void *arg)
  {
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
    case TINYUSB_EVENT_DETACHED:
    case TINYUSB_EVENT_SUSPENDED:
    case TINYUSB_EVENT_RESUMED:
    default:
        break;
    }
  }

  void main(void) {
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler);
    tinyusb_driver_install(&tusb_cfg);
  }
```

User Argument could be passed to the USB Device Event callback as a second argument (optional):

```c
  #include "tinyusb_default_config.h"

  static context_t *context;

  static void device_event_handler(tinyusb_event_t *event, void *arg)
  {
    context_t *context = (context_t*) arg;

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
    case TINYUSB_EVENT_DETACHED:
    case TINYUSB_EVENT_SUSPENDED:
    case TINYUSB_EVENT_RESUMED:
    default:
        break;
    }
  }

  void main(void) {
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler, (void*) context);
    tinyusb_driver_install(&tusb_cfg);
  }
```

### Suspend / Resume Device Events

Suspend and resume device events are **optional** and are disabled by default.
Users can choose one of the following approaches:

#### Option 1 — Use esp_tinyusb device events (recommended for integration)

Enable the following Kconfig options:

- `CONFIG_TINYUSB_SUSPEND_CALLBACK` → enables `TINYUSB_EVENT_SUSPENDED`
- `CONFIG_TINYUSB_RESUME_CALLBACK` → enables `TINYUSB_EVENT_RESUMED`

When enabled:

- esp_tinyusb provides strong implementations of:
  - `tud_suspend_cb()`
  - `tud_resume_cb()`
- esp_tinyusb dispatches suspend/resume events via the device event callback.

⚠️ **Important:**
When these options are enabled, user applications **MUST NOT** define
`tud_suspend_cb()` or `tud_resume_cb()` themselves. Doing so will result
in a linker error due to multiple definitions.

#### Option 2 — Use TinyUSB callbacks directly (default behavior)

If the Kconfig options are **disabled** (default):

- esp_tinyusb does NOT handle suspend/resume events
- Users may implement TinyUSB callbacks directly in their application:

```c
void tud_suspend_cb(bool remote_wakeup_en)
{
    // User suspend handling
}

void tud_resume_cb(void)
{
    // User resume handling
}
```

### Peripheral port

When several peripheral ports are available by the hardware, the specific port could be configured manually:

```c
  #include "tinyusb_default_config.h"

  void main(void) {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.port = TINYUSB_PORT_HIGH_SPEED_0;
#else
    tusb_cfg.port = TINYUSB_PORT_FULL_SPEED_0;
#endif
    tinyusb_driver_install(&tusb_cfg);
  }
```

### Task configuration

When the default parameters of the internal task should be changed:

```c
  #include "tinyusb_default_config.h"

  void main(void) {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.task = TINYUSB_TASK_CUSTOM(4096 /*size */, 4 /* priority */, 0 /* affinity: 0 - CPU0, 1 - CPU1 ... */);
    tinyusb_driver_install(&tusb_cfg);
  }
```

### USB Descriptors configuration

Configure USB descriptors using the `tinyusb_config_t` structure:

- `descriptor.device`
- `descriptor.string`
- `descriptor.full_speed_config`
- `descriptor.high_speed_config`
- `descriptor.qualifier`

If any descriptor field is set to `NULL`, default descriptor will be assigned during installation. Values of default descriptors could be configured via `menuconfig`.

```c
  #include "tinyusb_default_config.h"

  void main(void) {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.descriptor.device = &custom_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = custom_full_speed_configuration;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = custom_high_speed_configuration;
#endif // TUD_OPT_HIGH_SPEED

    tinyusb_driver_install(&tusb_cfg);
  }
```

### USB PHY configuration & Self-Powered Device

For self-powered devices, monitoring the VBUS voltage is required. To do this:

- Configure a GPIO pin as an input, using an external voltage divider or comparator to detect the VBUS state.
- Set `self_powered = true` and assign the VBUS monitor GPIO in the `tinyusb_config_t` structure.

```c
  #include "tinyusb_default_config.h"

  void main(void)
  {
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.phy.self_powered = true;
    tusb_cfg.phy.vbus_monitor_io = GPIO_NUM_0;

    tinyusb_driver_install(&tusb_cfg);
  }
```

If external PHY is used:

```c
  #include "tinyusb_default_config.h"
  #include "esp_private/usb_phy.h"

  void main(void)
  {
    // Initialize the USB PHY externally
    usb_new_phy(...);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.phy.skip_setup = true;

    tinyusb_driver_install(&tusb_cfg);
  }
```

## USB Device Classes: usage, installation & configuration

### USB Serial Device (CDC-ACM)

To enable USB Serial Device:

- select the option from `menuconfig`.
- initialize the USB Serial Device with `tusb_cdc_acm_init` and a `tinyusb_config_cdcacm_t` structure

```c
const tinyusb_config_cdcacm_t acm_cfg = {
  .cdc_port = TINYUSB_CDC_ACM_0,
  .rx_unread_buf_sz = 64,
  .callback_rx = NULL,
  .callback_rx_wanted_char = NULL,
  .callback_line_state_changed = NULL,
  .callback_line_coding_changed = NULL
};
tusb_cdc_acm_init(&acm_cfg);
```

Redirect standard I/O streams to USB with `esp_tusb_init_console` and revert with `esp_tusb_deinit_console`.

### USB Mass Storage Device (MSC)

To enable Mass Storage Device:

- select the option from `menuconfig`
- configure storage for MSC Device class: SPI Flash or SD/MMC (when supported by the hardware).

**SPI-Flash Storage**

```c
static wl_handle_t storage_init_spiflash(void)
{
  wl_handle_t wl;
  // Find partition
  // Mount Wear Levelling
  return wl;
}

void main(void)
{
  wl_handle_t wl_handle = storage_init_spiflash();

  tinyusb_msc_storage_handle_t storage_hdl;
  const tinyusb_msc_storage_config_t cfg = {
    .medium.wl_handle = wl_handle,
  };
  tinyusb_msc_new_storage_spiflash(&cfg, &storage_hdl);

  // After installing TinyUSB driver, MSC Class will have one LUN, mapped to SPI/Flash storage
}
```

**SD-Card Storage**

```c
static sdmmc_card_t *storage_init_sdmmc(void)
{
  sdmmc_card_t *card;
  // Config sdmmc
  // Init sdmmc_host
  // Init sdmmc slot
  // Init sdmmc card
  return card;
}

void main(void)
{
  sdmmc_card_t *card = storage_init_sdmmc();

  tinyusb_msc_storage_handle_t storage_hdl;
  const tinyusb_msc_storage_config_t cfg = {
    .medium.card = card,
  };
  tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);

  // After installing TinyUSB driver, MSC Class will have one LUN, mapped to SD/MMC storage
}
```

**Dual Storage**

```c
static wl_handle_t storage_init_spiflash(void)
{
  wl_handle_t wl;
  // Find partition
  // Mount Wear Levelling
  return wl;
}

static sdmmc_card_t *storage_init_sdmmc(void)
{
  sdmmc_card_t *card;
  // Config sdmmc
  // Init sdmmc_host
  // Init sdmmc slot
  // Init sdmmc card
  return card;
}

void main(void)
{
  tinyusb_msc_storage_handle_t storage1_hdl;
  tinyusb_msc_storage_handle_t storage2_hdl;
  tinyusb_msc_storage_config_t cfg;

  sdmmc_card_t *card = storage_init_sdmmc();
  wl_handle_t wl_handle = storage_init_spiflash();

  // Create SPI/Flash storage
  cfg.medium.wl_handle = wl_handle;
  tinyusb_msc_new_storage_spiflash(&cfg, &storage_hdl);

  // Create SD/MMC storage
  cfg.medium.card = card;
  tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);

  // After installing TinyUSB driver, MSC Class will have two LUNs, mapped to SPI/Flash and SD/MMC storages accordingly
}
```

**Storage callback**

Storage event callback is called, when one of the following events occurred:

- `TINYUSB_MSC_EVENT_MOUNT_START`: Start mount from APP to USB or from USB to APP
- `TINYUSB_MSC_EVENT_MOUNT_COMPLETE`: Complete mount from USB to APP or from APP to USB
- `TINYUSB_MSC_EVENT_FORMAT_REQUIRED`: Filesystem not found, format needed
- `TINYUSB_MSC_EVENT_MOUNT_FAILED`: Error occurred during mounting filesystem

To use or enable storage callback there are two options available.

Set the callback with specific call after creating the storage:

```c
static void storage_event_handle(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    switch (event->id) {
      case TINYUSB_MSC_EVENT_MOUNT_START:
      case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
      case TINYUSB_MSC_EVENT_MOUNT_FAILED:
      case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
    default:
        break;
    }
}

void main(void)
{
  sdmmc_card_t *card = storage_init_sdmmc();

  tinyusb_msc_storage_handle_t storage_hdl;
  const tinyusb_msc_storage_config_t cfg = {
    .medium.card = card,
  };
  tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);
  tinyusb_msc_set_storage_callback(storage_event_handle, NULL /* user argument for the event callback */);
}
```

Install the TinyUSB MSC Storage driver explicitly and provide the storage event via configuration:

```c
void main(void)
{
  sdmmc_card_t *card = storage_init_sdmmc();

  const tinyusb_msc_driver_config_t driver_cfg = {
    .storage_event_cb = storage_event_handle,
    .storage_event_arg = NULL /* user argument for the storage event callback */,
  };
  tinyusb_msc_install_driver(&driver_cfg);

  tinyusb_msc_storage_handle_t storage_hdl;
  const tinyusb_msc_storage_config_t cfg = {
    .medium.card = card,
  };
  tinyusb_msc_new_storage_sdmmc(&cfg, &storage_hdl);
}
```

### MSC Performance Optimization

- **Single-buffer approach:** Buffer size is set via `CONFIG_TINYUSB_MSC_BUFSIZE`.
- **Performance:** SD cards offer higher throughput than internal SPI flash due to architectural constraints.

**Performance Table (ESP32-S3):**

| FIFO Size | Read Speed | Write Speed |
| --------- | ---------- | ----------- |
| 512 B     | 0.566 MB/s | 0.236 MB/s  |
| 8192 B    | 0.925 MB/s | 0.928 MB/s  |

**Performance Table (ESP32-P4):**

| FIFO Size | Read Speed | Write Speed |
| --------- | ---------- | ----------- |
| 512 B     | 1.174 MB/s | 0.238 MB/s  |
| 8192 B    | 4.744 MB/s | 2.157 MB/s  |
| 32768 B   | 5.998 MB/s | 4.485 MB/s  |

**Performance Table (ESP32-S2, SPI Flash):**

| FIFO Size | Write Speed |
| --------- | ----------- |
| 512 B     | 5.59 KB/s   |
| 8192 B    | 21.54 KB/s  |

**Note:** Internal SPI flash is for demonstration only; use SD cards or external flash for higher performance.

## Examples

You can find examples in [ESP-IDF on GitHub](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device).
