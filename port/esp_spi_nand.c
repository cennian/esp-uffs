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

#include "esp_spi_nand.h"
#include "esp_heap_caps.h" // For runtime mock sizing check
#include "esp_log.h"
#include "esp_spi_nand_common.h"
#include "esp_spi_nand_types.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "uffs_spi_nand";

// Helper memory functions required by UFFS but specific to port
static void *uffs_spi_nand_malloc(struct uffs_DeviceSt *dev,
                                  unsigned int size) {
  return malloc(size);
}

static int uffs_spi_nand_free(struct uffs_DeviceSt *dev, void *p) {
  free(p);
  return 0;
}

static int uffs_spi_nand_device_init(uffs_Device *dev) {
  // This is called by UFFS when mounting
  if (dev->ops->InitFlash) {
    return dev->ops->InitFlash(dev);
  }
  return 0;
}

static int uffs_spi_nand_device_release(uffs_Device *dev) {
  if (dev->ops->ReleaseFlash) {
    dev->ops->ReleaseFlash(dev);
  }
  return 0;
}

// Generic/Fallback Driver
esp_err_t uffs_spi_nand_init_generic(struct uffs_DeviceSt *dev,
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

  // Best effort geometry
  priv->spi = spi;
  priv->page_size = 2048;
  priv->spare_size = 64;
  priv->block_size = 64;
  priv->block_size = 64;
#ifdef CONFIG_MOCK_FLASH_SIZE_BLOCKS
  // Runtime check matches mock driver logic
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 1024 * 1024) {
    priv->total_blocks = 1024;
  } else {
    priv->total_blocks = 128;
  }
#else
  priv->total_blocks = 1024; // Generic fallback size
#endif

  attr->page_data_size = priv->page_size;
  attr->pages_per_block = priv->block_size;
  attr->spare_size = priv->spare_size;
  attr->block_status_offs = 0;
  attr->ecc_opt = UFFS_ECC_NONE;
  attr->layout_opt = UFFS_LAYOUT_UFFS;
  attr->total_blocks = priv->total_blocks;
  attr->_private = priv;

  ops->InitFlash = NULL;
  ops->ReleaseFlash = NULL;
  ops->ReadPage = uffs_spi_nand_read_page_generic;
  ops->WritePage = uffs_spi_nand_write_page_generic;
  // We don't have a generic write_page_with_layout because it needs MakeSpare
  // which might be specific But we can use a simple one
  ops->EraseBlock = uffs_spi_nand_erase_block_generic;

  dev->attr = attr;
  dev->ops = ops;

  return ESP_OK;
}

// Driver Registry
static const spi_nand_driver_desc_t drivers[] = {
    {.mfr_id = 0xEF, .name = "Winbond", .init = uffs_spi_nand_init_winbond},
    {.mfr_id = 0xC8, .name = "GigaDevice", .init = uffs_spi_nand_init_gd},
    {.mfr_id = 0x2C, .name = "Micron", .init = uffs_spi_nand_init_micron},
    {.mfr_id = 0x52, .name = "Alliance", .init = uffs_spi_nand_init_alliance},
    {.mfr_id = 0xBA, .name = "Zetta", .init = uffs_spi_nand_init_zetta},
    {.mfr_id = 0x0B, .name = "XTX", .init = uffs_spi_nand_init_xtx},
};

esp_err_t esp_uffs_spi_nand_init(uffs_Device *dev,
                                 spi_device_handle_t spi_handle) {
  if (!dev || !spi_handle)
    return ESP_ERR_INVALID_ARG;

  // 1. Identify Device
  uint8_t id_data[3];
  uint8_t tx[2] = {CMD_READ_ID, 0x00};
  spi_transaction_t t = {
      .length = 16, .tx_buffer = tx, .rxlength = 16, .rx_buffer = id_data};

  // Use polling for ID read during init to be safe/simple
  spi_device_polling_transmit(spi_handle, &t);

  ESP_LOGI(TAG, "NAND ID: Mfr=0x%02X Dev=0x%02X", id_data[0], id_data[1]);

  esp_err_t ret = ESP_FAIL;
  bool driver_found = false;

  // 2. Find Driver
  for (size_t i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
    if (drivers[i].mfr_id == id_data[0]) {
      ESP_LOGI(TAG, "Detected %s Flash", drivers[i].name);
      ret = drivers[i].init(dev, spi_handle);
      driver_found = true;
      break;
    }
  }

  // 3. Fallback
  if (!driver_found) {
    ESP_LOGW(TAG, "Unknown Manufacturer ID 0x%02X, using generic driver",
             id_data[0]);
    ret = uffs_spi_nand_init_generic(dev, spi_handle);
  }

  if (ret == ESP_OK) {
    dev->Init = uffs_spi_nand_device_init;
    dev->Release = uffs_spi_nand_device_release;
    dev->mem.malloc = uffs_spi_nand_malloc;
    dev->mem.free = uffs_spi_nand_free;
  }
  return ret;
}
