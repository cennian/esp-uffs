#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"
#include "uffs/uffs_device.h"
#include "uffs/uffs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the UFFS device structure for a generic SPI NAND
 *
 * This function sets up the uffs_Device callbacks to point to the SPI NAND
 * driver implementation. It attempts to identify the connected NAND chip
 * (Winbond, GigaDevice, etc.) and configure generic SPI NAND parameters
 * accordingly.
 *
 * @param dev Pointer to the uffs_Device structure to initialize.
 * @param spi_handle Handle to the initialized SPI device (from
 * spi_bus_add_device). The standard configuration should be:
 *                   - Mode 0 or 3
 *                   - CS handled by driver or manually (we use driver here)
 *                   - Clock speed up to device limit (usually 20-40MHz or more)
 * @return esp_err_t ESP_OK on success, ESP_FAIL if detection fails or invalid
 * parameters.
 */
esp_err_t esp_uffs_spi_nand_init(uffs_Device *dev,
                                 spi_device_handle_t spi_handle);

#ifdef __cplusplus
}
#endif
