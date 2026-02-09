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

static const char *TAG = "uffs_winbond";

static int uffs_winbond_init_flash(struct uffs_DeviceSt *dev) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint8_t cmd = CMD_RESET;
  spi_nand_op(priv->spi, &cmd, 1, NULL, 0);
  spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, NULL);

  // Winbond specific: Unprotect blocks (Global Unlock)
  // Write 0x00 to Status Register-1 (Addr 0xA0) to clear protection
  uint8_t status_reg_1 = 0x00;
  uint8_t cmd_wr[3] = {CMD_SET_FEATURE, REG_BLOCK_LOCK, status_reg_1};
  spi_nand_op(priv->spi, cmd_wr, 3, NULL, 0);

  return 0;
}

static int uffs_winbond_release_flash(struct uffs_DeviceSt *dev) { return 0; }

// Winbond uses generic implementation for read/write
// But if specific ECC handling is needed, overrides go here.

static int uffs_winbond_write_page_with_layout(
    struct uffs_DeviceSt *dev, u32 block, u32 page, const uint8_t *data,
    int data_len, const uint8_t *ecc, const struct uffs_TagStoreSt *ts) {
  uint8_t spare[64];
  memset(spare, 0xFF, sizeof(spare));

  if (ts) {
    uffs_FlashMakeSpare(dev, ts, ecc, spare);
  }

  return uffs_spi_nand_write_page_generic(dev, block, page, data, data_len,
                                          spare, sizeof(spare));
}

esp_err_t uffs_spi_nand_init_winbond(struct uffs_DeviceSt *dev,
                                     spi_device_handle_t spi) {
  if (!dev || !spi)
    return ESP_ERR_INVALID_ARG;

  // Allocate structs
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

  // Geometry for standard Winbond (e.g. W25N01GV) calling it 128MB
  priv->spi = spi;
  priv->page_size = 2048;
  priv->spare_size = 64;
  priv->block_size = 64;
#ifdef CONFIG_MOCK_FLASH_SIZE_BLOCKS
  priv->total_blocks = CONFIG_MOCK_FLASH_SIZE_BLOCKS;
#else
  priv->total_blocks = 1024; // Standard W25N01GV size
#endif

  attr->page_data_size = priv->page_size;
  attr->pages_per_block = priv->block_size;
  attr->spare_size = priv->spare_size;
  attr->block_status_offs = 0; // First byte of spare (Bad Block Marker)
  attr->ecc_opt = UFFS_ECC_HW_AUTO;
  attr->layout_opt = UFFS_LAYOUT_UFFS;
  attr->total_blocks = priv->total_blocks;
  attr->_private = priv;

  ops->InitFlash = uffs_winbond_init_flash;
  ops->ReleaseFlash = uffs_winbond_release_flash;
  ops->ReadPage = uffs_spi_nand_read_page_generic;
  ops->WritePage = uffs_spi_nand_write_page_generic;
  ops->WritePageWithLayout = uffs_winbond_write_page_with_layout;
  ops->EraseBlock = uffs_spi_nand_erase_block_generic;

  dev->attr = attr;
  dev->ops = ops;

  // We assume dev->Init/Release/mem are handled by the caller or common logic?
  // The original code did it in init. Let's do it here or in common.
  // Ideally the 'ops' and 'attr' are the main thing the driver provides.

  return ESP_OK;
}
