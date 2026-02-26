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

#if (SOC_SDMMC_HOST_SUPPORTED)
/**
 * @brief Initialize the SDMMC storage
 *
 * This function initializes the SDMMC host and card, setting up the necessary configurations.
 *
 * @param[out] card Pointer to a pointer that will hold the initialized sdmmc_card_t structure
 */
void storage_init_sdmmc(sdmmc_card_t **card);

/**
 * @brief Erase the SDMMC storage
 *
 * This function erases the entire SDMMC card.
 *
 * @param[in] card Pointer to the sdmmc_card_t structure that was initialized in storage_init_sdmmc
 */
void storage_erase_sdmmc(sdmmc_card_t *card);

/**
 * @brief Deinitialize the SDMMC storage
 *
 * @param[in] card Pointer to the sdmmc_card_t structure that was initialized in storage_init_sdmmc
 */
void storage_deinit_sdmmc(sdmmc_card_t *card);
#endif // SOC_SDMMC_HOST_SUPPORTED
