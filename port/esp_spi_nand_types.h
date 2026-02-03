#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"
#include "uffs/uffs_device.h"
#include "uffs/uffs_flash.h"
// stdlib for calloc/free is used in implementation, but here we just need
// types.
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
struct uffs_DeviceSt;

/**
 * @brief Chip-specific initialization function type
 *
 * @param dev Pointer to uffs_Device
 * @param spi SPI device handle
 * @return esp_err_t ESP_OK on success
 */
typedef esp_err_t (*spi_nand_chip_init_fn)(struct uffs_DeviceSt *dev,
                                           spi_device_handle_t spi);

/**
 * @brief Descriptor for a SPI NAND chip driver
 */
typedef struct {
  uint8_t mfr_id;             // Manufacturer ID (e.g., 0xEF for Winbond)
  const char *name;           // Human-readable name
  spi_nand_chip_init_fn init; // Initialization function
} spi_nand_driver_desc_t;

// Exposed functions from chip drivers
esp_err_t uffs_spi_nand_init_winbond(struct uffs_DeviceSt *dev,
                                     spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_gd(struct uffs_DeviceSt *dev,
                                spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_generic(struct uffs_DeviceSt *dev,
                                     spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_micron(struct uffs_DeviceSt *dev,
                                    spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_alliance(struct uffs_DeviceSt *dev,
                                      spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_zetta(struct uffs_DeviceSt *dev,
                                   spi_device_handle_t spi);
esp_err_t uffs_spi_nand_init_xtx(struct uffs_DeviceSt *dev,
                                 spi_device_handle_t spi);

#ifdef __cplusplus
}
#endif
