/*
 * SPDX-FileCopyrightText: 2020-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "tinyusb.h"
#include "tinyusb_task.h"
#include "tusb.h"

#if (CONFIG_TINYUSB_MSC_ENABLED)
#include "tinyusb_msc.h"
#include "msc_storage.h"
#endif // CONFIG_TINYUSB_MSC_ENABLED

const static char *TAG = "TinyUSB";

/**
 * @brief TinyUSB context
 */
typedef struct {
    tinyusb_port_t port;                      /*!< USB Peripheral hardware port number. Available when hardware has several available peripherals. */
    usb_phy_handle_t phy_hdl;                 /*!< USB PHY handle */
    tinyusb_event_cb_t event_cb;              /*!< Callback function that will be called when USB events occur. */
    void *event_arg;                          /*!< Pointer to the argument passed to the callback */
    bool remote_wakeup_en;                    /*!< Remote wakeup enabled flag */
} tinyusb_ctx_t;

static tinyusb_ctx_t s_ctx; // TinyUSB context

// ==================================================================================
// ============================= TinyUSB Callbacks ==================================
// ==================================================================================

/**
 * @brief Callback function invoked when device is mounted (configured)
 *
 * This function is called by TinyUSB stack when:
 *
 * - SetConfiguration(n) is called by the host, where n is the configuration number and not zero.
 *
 * @note
 * For Win-based Hosts: SetConfiguration(n) request is present only with available Class in Device Descriptor.
 */
void tud_mount_cb(void)
{
#if (CONFIG_TINYUSB_MSC_ENABLED)
    msc_storage_mount_to_usb();
#endif // CONFIG_TINYUSB_MSC_ENABLED
    tinyusb_event_t event = {
        .id = TINYUSB_EVENT_ATTACHED,
        .rhport = s_ctx.port,
    };

    if (s_ctx.event_cb) {
        s_ctx.event_cb(&event, s_ctx.event_arg);
    }
}

/**
 * @brief Callback function invoked when device is unmounted
 *
 * This function is called by TinyUSB stack when:
 *
 * - SetConfiguration(0) is called by the host.
 * - Device is disconnected (DCD_EVENT_UNPLUGGED) from the host.
 */
void tud_umount_cb(void)
{
#if (CONFIG_TINYUSB_MSC_ENABLED)
    msc_storage_mount_to_app();
#endif // CONFIG_TINYUSB_MSC_ENABLED
    tinyusb_event_t event = {
        .id = TINYUSB_EVENT_DETACHED,
        .rhport = s_ctx.port,
    };

    if (s_ctx.event_cb) {
        s_ctx.event_cb(&event, s_ctx.event_arg);
    }
}

#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
/**
 * @brief Callback function invoked when device is suspended
 *
 * This function is called by TinyUSB stack when:
 *
 * - Host suspends the root port
 *
 * @param[in] remote_wakeup_en Remote wakeup is currently enabled/disabled on the device
 */
void tud_suspend_cb(bool remote_wakeup_en)
{
    tinyusb_event_t event = {
        .id = TINYUSB_EVENT_SUSPENDED,
        .rhport = s_ctx.port,
        .suspended = {
            .remote_wakeup = remote_wakeup_en,
        },
    };

    // Save the remote wakeup enabled flag
    s_ctx.remote_wakeup_en = remote_wakeup_en;

    if (s_ctx.event_cb) {
        s_ctx.event_cb(&event, s_ctx.event_arg);
    }
}
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
/**
 * @brief Callback function invoked when device is resumed
 *
 * This function is called by TinyUSB stack when:
 *
 * - Host resumes the the root port
 */
void tud_resume_cb(void)
{
    tinyusb_event_t event = {
        .id = TINYUSB_EVENT_RESUMED,
        .rhport = s_ctx.port,
    };

    if (s_ctx.event_cb) {
        s_ctx.event_cb(&event, s_ctx.event_arg);
    }
}
#endif // CONFIG_TINYUSB_RESUME_CALLBACK

// ==================================================================================
// ============================= ESP TinyUSB Driver =================================
// ==================================================================================

/**
 * @brief Check the TinyUSB configuration
 */
static esp_err_t tinyusb_check_config(const tinyusb_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Config can't be NULL");
    ESP_RETURN_ON_FALSE(config->port < TINYUSB_PORT_MAX, ESP_ERR_INVALID_ARG, TAG, "Port number should be supported by the hardware");
#if (CONFIG_IDF_TARGET_ESP32P4)
#ifndef USB_PHY_SUPPORTS_P4_OTG11
    ESP_RETURN_ON_FALSE(config->port != TINYUSB_PORT_0, ESP_ERR_INVALID_ARG, TAG, "USB PHY support for OTG1.1 has not been implemented, please update your esp-idf");
#endif // ESP-IDF supports OTG1.1 peripheral
#endif // CONFIG_IDF_TARGET_ESP32P4
    return ESP_OK;
}

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config)
{
    ESP_RETURN_ON_ERROR(tinyusb_check_config(config), TAG, "TinyUSB configuration check failed");
    ESP_RETURN_ON_ERROR(tinyusb_task_check_config(&config->task), TAG, "TinyUSB task configuration check failed");

    esp_err_t ret;
    usb_phy_handle_t phy_hdl = NULL;
    if (!config->phy.skip_setup) {
        // Configure USB PHY
        usb_phy_config_t phy_conf = {
            .controller = USB_PHY_CTRL_OTG,
            .target = USB_PHY_TARGET_INT,
            .otg_mode = USB_OTG_MODE_DEVICE,
            .otg_speed = USB_PHY_SPEED_FULL,
        };

#if (SOC_USB_OTG_PERIPH_NUM > 1)
        if (config->port == TINYUSB_PORT_HIGH_SPEED_0) {
            // Default PHY for OTG2.0 is UTMI
            phy_conf.target = USB_PHY_TARGET_UTMI;
            phy_conf.otg_speed = USB_PHY_SPEED_HIGH;
        }
#endif // (SOC_USB_OTG_PERIPH_NUM > 1)

        // OTG IOs config
        const usb_phy_otg_io_conf_t otg_io_conf = USB_PHY_SELF_POWERED_DEVICE(config->phy.vbus_monitor_io);
        if (config->phy.self_powered) {
            phy_conf.otg_io_conf = &otg_io_conf;
        }
        ESP_RETURN_ON_ERROR(usb_new_phy(&phy_conf, &phy_hdl), TAG, "Install USB PHY failed");
    }
    // Init TinyUSB stack in task
    ESP_GOTO_ON_ERROR(tinyusb_task_start(config->port, &config->task, &config->descriptor), del_phy, TAG, "Init TinyUSB task failed");

    s_ctx.port = config->port;              // Save the port number
    s_ctx.phy_hdl = phy_hdl;                // Save the PHY handle for uninstallation
    s_ctx.event_cb = config->event_cb;      // Save the event callback
    s_ctx.event_arg = config->event_arg;    // Save the event callback argument
    s_ctx.remote_wakeup_en = false;         // Remote wakeup is disabled by default

    ESP_LOGI(TAG, "TinyUSB Driver installed on port %d", config->port);
    return ESP_OK;

del_phy:
    if (!config->phy.skip_setup) {
        usb_del_phy(phy_hdl);
    }
    return ret;
}

esp_err_t tinyusb_driver_uninstall(void)
{
    ESP_RETURN_ON_ERROR(tinyusb_task_stop(), TAG, "Deinit TinyUSB task failed");
    if (s_ctx.phy_hdl) {
        ESP_RETURN_ON_ERROR(usb_del_phy(s_ctx.phy_hdl), TAG, "Unable to delete PHY");
        s_ctx.phy_hdl = NULL;
    }
    return ESP_OK;
}

esp_err_t tinyusb_remote_wakeup(void)
{
    // Check if the remote wakeup flag was set by the esp_tinyusb's suspend callback
    // In case of user-defined suspend callback, user manages remote wakeup capability on it's own
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    ESP_RETURN_ON_FALSE(s_ctx.remote_wakeup_en, ESP_ERR_INVALID_STATE, TAG, "Remote wakeup is not enabled by the host");
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

    ESP_RETURN_ON_FALSE(tud_remote_wakeup(), ESP_FAIL, TAG, "Remote wakeup request failed");

#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    s_ctx.remote_wakeup_en = false; // Remote wakeup can be used only once, disable it until next suspend
#endif // CONFIG_TINYUSB_SUSPEND_CALLBACK

    return ESP_OK;
}
