#include "esp_log.h"
#include "esp_spi_nand_common.h"
#include "esp_spi_nand_types.h"
#include <string.h>

static const char *TAG = "uffs_gd";

// GD Specific ECC Mask and Values
// Status Register 1 (0xC0)
// Bit 4-6: ECC Status
// 111 (7): Uncorrectable
// Others: Correctable or OK
#define GD_SR_ECC_MASK 0x70

static int uffs_gd_init_flash(struct uffs_DeviceSt *dev) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint8_t cmd = CMD_RESET;
  spi_nand_op(priv->spi, &cmd, 1, NULL, 0);
  spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, NULL);

  // GD specific: Unprotect blocks
  uint8_t status_reg_1 = 0x00;
  uint8_t cmd_wr[3] = {CMD_SET_FEATURE, REG_BLOCK_LOCK, status_reg_1};
  spi_nand_op(priv->spi, cmd_wr, 3, NULL, 0);

  return 0;
}

static int uffs_gd_read_page(struct uffs_DeviceSt *dev, uint32_t block,
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

  // 2. Wait for Load
  uint8_t status = 0;
  if (spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, &status) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 3. Check ECC Status (GD Specific)
  int ecc_res = UFFS_FLASH_NO_ERR;
  int ecc_stat = (status & GD_SR_ECC_MASK) >> 4;

  if (ecc_stat == 7) { // Uncorrectable (111b)
    ESP_LOGE(TAG, "ECC Uncorrectable Error at Blk %u Pg %u",
             (unsigned int)block, (unsigned int)page);
    return UFFS_FLASH_ECC_FAIL;
  } else if (ecc_stat > 0) {
    // 1..6 are corrected errors
    ecc_res = UFFS_FLASH_ECC_OK;
  }

  // 4. READ FROM CACHE
  // Read Data
  if (data && data_len > 0) {
    uint8_t cmd_cache[4] = {CMD_READ_CACHE, 0, 0, 0};
    spi_transaction_t t = {.length = 32,
                           .tx_buffer = cmd_cache,
                           .rxlength = (size_t)data_len * 8,
                           .rx_buffer = data};
    if (spi_device_transmit(priv->spi, &t) != ESP_OK)
      return UFFS_FLASH_IO_ERR;
  }

  // Read Spare
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

static int uffs_gd_write_page_with_layout(struct uffs_DeviceSt *dev,
                                          uint32_t block, uint32_t page,
                                          const uint8_t *data, int data_len,
                                          const uint8_t *ecc,
                                          const struct uffs_TagStoreSt *ts) {
  uint8_t spare[64];
  memset(spare, 0xFF, sizeof(spare));

  if (ts) {
    uffs_FlashMakeSpare(dev, ts, ecc, spare);
  }
  return uffs_spi_nand_write_page_generic(dev, block, page, data, data_len,
                                          spare, sizeof(spare));
}

esp_err_t uffs_spi_nand_init_gd(struct uffs_DeviceSt *dev,
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

  ops->InitFlash = uffs_gd_init_flash;
  ops->ReleaseFlash = NULL;          // Optional
  ops->ReadPage = uffs_gd_read_page; // Custom ECC check
  ops->WritePage = uffs_spi_nand_write_page_generic;
  ops->WritePageWithLayout = uffs_gd_write_page_with_layout;
  ops->EraseBlock = uffs_spi_nand_erase_block_generic;

  dev->attr = attr;
  dev->ops = ops;

  return ESP_OK;
}
