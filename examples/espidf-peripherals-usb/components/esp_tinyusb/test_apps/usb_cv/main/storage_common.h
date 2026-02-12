/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "wear_levelling.h"

#if (SOC_SDMMC_HOST_SUPPORTED)
#include "sdmmc_cmd.h"
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Initialize the SPIFLASH storage with wear levelling
 *
 * @param[out] wl_handle Pointer to the wear levelling handle that will be initialized
 */
void storage_init_spiflash(wl_handle_t *wl_handle);

/**
 * @brief Erase the SPIFLASH storage partition
 *
 * This function erases the entire SPIFLASH storage partition.
 *
 * @note There is no protection against accidental data loss and collision with wear levelling, use with caution.
 */
void storage_erase_spiflash(void);

/**
 * @brief Deinitialize the SPIFLASH storage and unmount wear levelling
 *
 * @param[in] wl_handle Wear levelling handle, obtained from storage_init_spiflash
 */
void storage_deinit_spiflash(wl_handle_t wl_handle);
