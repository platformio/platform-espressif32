/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdio.h>
#include <libusb-1.0/libusb.h>

#define TINYUSB_VENDOR                  0x303A
#define TINYUSB_PRODUCT                 0x4002

#define DESC_TYPE_DEVICE_QUALIFIER      0x06
#define DESC_TYOE_OTHER_SPEED_CONFIG    0x07

// Buffer for descriptor data
unsigned char buffer[512] = { 0 };

// USB Other Speed Configuration Descriptor
typedef struct  __attribute__ ((packed))
{
    uint8_t  bLength             ; ///< Size of descriptor
    uint8_t  bDescriptorType     ; ///< Other_speed_Configuration Type
    uint16_t wTotalLength        ; ///< Total length of data returned

    uint8_t  bNumInterfaces      ; ///< Number of interfaces supported by this speed configuration
    uint8_t  bConfigurationValue ; ///< Value to use to select configuration
    uint8_t  iConfiguration      ; ///< Index of string descriptor
    uint8_t  bmAttributes        ; ///< Same as Configuration descriptor
    uint8_t  bMaxPower           ; ///< Same as Configuration descriptor
} desc_other_speed_t;

// USB Device Qualifier Descriptor
typedef struct  __attribute__ ((packed))
{
    uint8_t  bLength            ; ///< Size of descriptor
    uint8_t  bDescriptorType    ; ///< Device Qualifier Type
    uint16_t bcdUSB             ; ///< USB specification version number (e.g., 0200H for V2.00)

    uint8_t  bDeviceClass       ; ///< Class Code
    uint8_t  bDeviceSubClass    ; ///< SubClass Code
    uint8_t  bDeviceProtocol    ; ///< Protocol Code

    uint8_t  bMaxPacketSize0    ; ///< Maximum packet size for other speed
    uint8_t  bNumConfigurations ; ///< Number of Other-speed Configurations
    uint8_t  bReserved          ; ///< Reserved for future use, must be zero
} desc_device_qualifier_t;

// printf helpers
static void _print_device_qulifier_desc(unsigned char *buffer, int length);
static void _print_other_speed_desc(unsigned char *buffer, int length);

//
// MAIN
//
int main()
{
    libusb_context *context = NULL;
    int rc = 0;

    rc = libusb_init(&context);
    assert(rc == 0);
    libusb_device_handle *dev_handle = libusb_open_device_with_vid_pid(context,
                                       TINYUSB_VENDOR,
                                       TINYUSB_PRODUCT);

    if (dev_handle != NULL) {
        printf("TinyUSB Device has been found\n");

        // Test Qualifier Descriprtor
        // 1. Get Qualifier Descriptor
        // 2. print descriptor data
        rc = libusb_get_descriptor(dev_handle, DESC_TYPE_DEVICE_QUALIFIER, 0, buffer, 512);
        _print_device_qulifier_desc(buffer, rc);

        // Test Other Speed Descriptor
        // 1. Get Other Speed Descriptor
        // 2. print descriptor data
        rc = libusb_get_descriptor(dev_handle, DESC_TYOE_OTHER_SPEED_CONFIG, 0, buffer, 512);
        _print_other_speed_desc(buffer, rc);

        libusb_close(dev_handle);
    } else {
        printf("TinyUSB Device has NOT been found\n");
    }

    libusb_exit(context);
}


// =============================================================================
static void _print_device_qulifier_desc(unsigned char *buffer, int length)
{
    assert(buffer);
    desc_device_qualifier_t *qualifier_desc = (desc_device_qualifier_t *) buffer;
    printf("========= Device Qualifier ========== \n");
    printf("\t bLength: %d \n", qualifier_desc->bLength);
    printf("\t bDescriptorType: %d (%#x)\n", qualifier_desc->bDescriptorType, qualifier_desc->bDescriptorType);
    printf("\t bcdUSB: %d (%#x) \n", qualifier_desc->bcdUSB, qualifier_desc->bcdUSB);
    printf("\t bDeviceClass: %d (%#x) \n", qualifier_desc->bDeviceClass, qualifier_desc->bDeviceClass);
    printf("\t bDeviceSubClass: %d \n", qualifier_desc->bDeviceSubClass);
    printf("\t bDeviceProtocol: %d \n", qualifier_desc->bDeviceProtocol);
    printf("\t bMaxPacketSize0: %d \n", qualifier_desc->bMaxPacketSize0);
    printf("\t bNumConfigurations: %d \n", qualifier_desc->bNumConfigurations);
}

static void _print_other_speed_desc(unsigned char *buffer, int length)
{
    assert(buffer);
    desc_other_speed_t *other_speed = (desc_other_speed_t *) buffer;
    printf("============ Other Speed ============ \n");
    printf("\t bLength: %d \n", other_speed->bLength);
    printf("\t bDescriptorType: %d (%#x) \n", other_speed->bDescriptorType, other_speed->bDescriptorType);
    printf("\t wTotalLength: %d \n", other_speed->wTotalLength);
    printf("\t bNumInterfaces: %d \n", other_speed->bNumInterfaces);
    printf("\t bConfigurationValue: %d \n", other_speed->bConfigurationValue);
    printf("\t iConfiguration: %d \n", other_speed->iConfiguration);
    printf("\t bmAttributes: %d (%#x) \n", other_speed->bmAttributes, other_speed->bmAttributes);
    printf("\t bMaxPower: %d (%#x) \n", other_speed->bMaxPower, other_speed->bMaxPower);
}
