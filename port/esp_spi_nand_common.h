#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"
#include "uffs/uffs_device.h"

#ifdef __cplusplus
extern "C" {
#endif

// SPI NAND Commands (Generic ONFI)
#define CMD_RESET 0xFF
#define CMD_GET_FEATURE 0x0F
#define CMD_SET_FEATURE 0x1F
#define CMD_READ_ID 0x9F
#define CMD_PAGE_READ 0x13       // Read page to cache
#define CMD_READ_CACHE 0x03      // Read from cache
#define CMD_READ_CACHE_FAST 0x0B // Read from cache fast
#define CMD_WRITE_ENABLE 0x06
#define CMD_WRITE_DISABLE 0x04
#define CMD_PROGRAM_LOAD 0x02    // Load data to cache
#define CMD_PROGRAM_EXECUTE 0x10 // Program cache to page
#define CMD_BLOCK_ERASE 0xD8

// Status Register Addresses
#define REG_STATUS 0xC0
#define REG_BLOCK_LOCK 0xA0

// Status Register Bits
#define SR_BUSY (1 << 0)   // OIP (Operation In Progress)
#define SR_WEL (1 << 1)    // Write Enable Latch
#define SR_E_FAIL (1 << 2) // Erase Fail
#define SR_P_FAIL (1 << 3) // Program Fail
#define SR_ECC_MASK 0x30   // ECC Status Mask (varies slightly)

// Timeout configuration
#define NAND_TIMEOUT_MS 500

// Internal Private Data Structure
typedef struct {
  spi_device_handle_t spi;
  uint32_t page_size;
  uint32_t spare_size;
  uint32_t block_size; // pages per block
  uint32_t total_blocks;
} spi_nand_priv_t;

// Common Helpers
esp_err_t spi_nand_op(spi_device_handle_t spi, const uint8_t *tx_data,
                      size_t tx_len, uint8_t *rx_data, size_t rx_len);

esp_err_t spi_nand_wait_busy(spi_device_handle_t spi, uint32_t timeout_ms,
                             uint8_t *status_out);

esp_err_t spi_nand_write_enable(spi_device_handle_t spi);

// Common UFFS Hooks (Generic implementation)
int uffs_spi_nand_read_page_generic(struct uffs_DeviceSt *dev, uint32_t block,
                                    uint32_t page, uint8_t *data, int data_len,
                                    uint8_t *ecc, uint8_t *spare,
                                    int spare_len);

int uffs_spi_nand_write_page_generic(struct uffs_DeviceSt *dev, uint32_t block,
                                     uint32_t page, const uint8_t *data,
                                     int data_len, const uint8_t *spare,
                                     int spare_len);

int uffs_spi_nand_erase_block_generic(struct uffs_DeviceSt *dev,
                                      uint32_t block);

#ifdef __cplusplus
}
#endif
