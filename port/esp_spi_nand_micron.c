/*
 * Copyright (C) 2024 Ihtesham Ullah
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "esp_log.h"
#include "esp_spi_nand_common.h"
#include "esp_spi_nand_types.h"
#include <string.h>

static const char *TAG = "uffs_micron";

// Micron uses 3 bits for ECC (Bits 4,5,6 of Status Register)
// 000: No errors
// 001: 1-3 bits corrected
// 010: Bit errors > 8 bits (Rewrite recommended) ?? Need to verify specific
// datasheet 011: Errors corrected (Rewrite recommended) 111: Uncorrectable For
// simplicity, we treat any non-zero as corrected (except uncorrectable) Mask:
// 0111 0000 = 0x70
#define MICRON_ECC_MASK 0x70
#define MICRON_ECC_UNCORRECTABLE                                               \
  0x07 // 111b ??? usually uncorrectable is high val.
// Actually Micron MT29F:
// 000: Clean
// 001: 1-bit correct
// 011: > 1-bit correct (E.g. threshold reached)
// 010: Error > ECC capability (Uncorrectable) - Wait, Micron is often 010 for
// Fail? Let's check GigaDevice: GD is 111 for uncorrectable. To be safe, I'll
// rely on the standard "Bit 0 of ECC status usually means corrected, Bit 2
// means bad" logic might not hold. Standard ONFI often: 2 bits (00, 01,
// 10-uncorr, 11-corr). Micron 2Gb SPI NAND (Get Feature 0xC0): Bit 4-6: 000:
// Normal 001: 1-3 bit error corrected 011: >= 4 bit error corrected (rewrite
// recomm) 010: Uncorrectable (Bit 5 set!) NOTE: 010 (2) is uncorrectable.

static int uffs_micron_init_flash(struct uffs_DeviceSt *dev) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint8_t cmd = CMD_RESET;
  spi_nand_op(priv->spi, &cmd, 1, NULL, 0);
  spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, NULL);

  // Block Unlock (Set REG_BLOCK_LOCK to 0)
  uint8_t status_reg_1 = 0x00;
  uint8_t cmd_wr[3] = {CMD_SET_FEATURE, REG_BLOCK_LOCK, status_reg_1};
  spi_nand_op(priv->spi, cmd_wr, 3, NULL, 0);

  return 0;
}

static int uffs_micron_read_page(struct uffs_DeviceSt *dev, uint32_t block,
                                 uint32_t page, uint8_t *data, int data_len,
                                 uint8_t *ecc, uint8_t *spare, int spare_len) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint32_t page_addr = block * priv->block_size + page;

  // 1. PAGE READ to Cache
  uint8_t cmd_read[4];
  cmd_read[0] = CMD_PAGE_READ;
  cmd_read[1] = (page_addr >> 16) & 0xFF;
  cmd_read[2] = (page_addr >> 8) & 0xFF;
  cmd_read[3] = page_addr & 0xFF;

  if (spi_nand_op(priv->spi, cmd_read, 4, NULL, 0) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 2. Wait
  uint8_t status = 0;
  if (spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, &status) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 3. Check ECC
  int ecc_res = UFFS_FLASH_NO_ERR;
  int ecc_stat = (status & MICRON_ECC_MASK) >> 4;

  // Mapping based on common Micron datasheets:
  // 0 (000): No Error
  // 1 (001): Corrected
  // 3 (011): Corrected (Rewrite)
  // 2 (010): Uncorrectable
  // 4-7: Reserved/Other

  if (ecc_stat == 2) {
    ESP_LOGE(TAG, "ECC Uncorrectable at Blk %u Pg %u (Stat: 0x%02X)",
             (unsigned int)block, (unsigned int)page, status);
    return UFFS_FLASH_ECC_FAIL;
  } else if (ecc_stat != 0) {
    ecc_res = UFFS_FLASH_ECC_OK;
  }

  // 4. READ FROM CACHE
  if (data && data_len > 0) {
    uint8_t cmd_cache[4] = {CMD_READ_CACHE, 0, 0, 0};
    spi_transaction_t t = {.length = 32,
                           .tx_buffer = cmd_cache,
                           .rxlength = (size_t)data_len * 8,
                           .rx_buffer = data};
    if (spi_device_transmit(priv->spi, &t) != ESP_OK)
      return UFFS_FLASH_IO_ERR;
  }
  if (spare && spare_len > 0) {
    uint16_t col = priv->page_size;
    uint8_t cmd_cache[4] = {CMD_READ_CACHE, (col >> 8) & 0xFF, col & 0xFF, 0};
    spi_transaction_t t = {.length = 32,
                           .tx_buffer = cmd_cache,
                           .rxlength = (size_t)spare_len * 8,
                           .rx_buffer = spare};
    if (spi_device_transmit(priv->spi, &t) != ESP_OK)
      return UFFS_FLASH_IO_ERR;
  }

  return ecc_res;
}

static int
uffs_micron_write_page_with_layout(struct uffs_DeviceSt *dev, uint32_t block,
                                   uint32_t page, const uint8_t *data,
                                   int data_len, const uint8_t *ecc,
                                   const struct uffs_TagStoreSt *ts) {
  uint8_t spare[64];
  memset(spare, 0xFF, sizeof(spare));
  if (ts)
    uffs_FlashMakeSpare(dev, ts, ecc, spare);
  return uffs_spi_nand_write_page_generic(dev, block, page, data, data_len,
                                          spare, sizeof(spare));
}

esp_err_t uffs_spi_nand_init_micron(struct uffs_DeviceSt *dev,
                                    spi_device_handle_t spi) {
  if (!dev || !spi)
    return ESP_ERR_INVALID_ARG;

  struct uffs_StorageAttrSt *attr =
      calloc(1, sizeof(struct uffs_StorageAttrSt));
  struct uffs_FlashOpsSt *ops = calloc(1, sizeof(struct uffs_FlashOpsSt));
  spi_nand_priv_t *priv = calloc(1, sizeof(spi_nand_priv_t));
  if (!attr || !ops || !priv) {
    free(attr);
    free(ops);
    free(priv);
    return ESP_ERR_NO_MEM;
  }

  priv->spi = spi;
  priv->page_size = 2048;
  priv->spare_size = 64;
  priv->block_size = 64;
  priv->total_blocks = 1024;

  attr->page_data_size = priv->page_size;
  attr->pages_per_block = priv->block_size;
  attr->spare_size = priv->spare_size;
  attr->block_status_offs = 0;
  attr->ecc_opt = UFFS_ECC_HW_AUTO;
  attr->layout_opt = UFFS_LAYOUT_UFFS;
  attr->total_blocks = priv->total_blocks;
  attr->_private = priv;

  ops->InitFlash = uffs_micron_init_flash;
  ops->ReadPage = uffs_micron_read_page;
  ops->WritePage = uffs_spi_nand_write_page_generic;
  ops->WritePageWithLayout = uffs_micron_write_page_with_layout;
  ops->EraseBlock = uffs_spi_nand_erase_block_generic;

  dev->attr = attr;
  dev->ops = ops;
  return ESP_OK;
}
