#include "esp_spi_nand_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uffs/uffs_device.h"
#include "uffs/uffs_flash.h"
#include <string.h>

static const char *TAG = "uffs_nand_common";

esp_err_t spi_nand_op(spi_device_handle_t spi, const uint8_t *tx_data,
                      size_t tx_len, uint8_t *rx_data, size_t rx_len) {
  if (tx_len == 0 && rx_len == 0)
    return ESP_OK;

  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = tx_len * 8;
  t.tx_buffer = tx_data;
  t.rxlength = rx_len * 8;
  t.rx_buffer = rx_data;

  return spi_device_transmit(spi, &t);
}

esp_err_t spi_nand_wait_busy(spi_device_handle_t spi, uint32_t timeout_ms,
                             uint8_t *status_out) {
  uint8_t cmd[] = {CMD_GET_FEATURE, REG_STATUS};
  uint8_t status;
  uint32_t start = xTaskGetTickCount(); // Using ticks for timeout

  while (1) {
    spi_transaction_t t = {
        .length = 16, // 8 bit cmd + 8 bit addr
        .tx_buffer = cmd,
        .rxlength = 8, // 8 bit status
        .rx_buffer = &status,
    };
    esp_err_t ret = spi_device_polling_transmit(spi, &t);
    if (ret != ESP_OK)
      return ret;

    if (!(status & SR_BUSY)) {
      if (status_out)
        *status_out = status;
      return ESP_OK;
    }

    if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS > timeout_ms) {
      ESP_LOGE(TAG, "NAND Busy Timeout! Status: 0x%02X", status);
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(1); // 1 tick
  }
}

esp_err_t spi_nand_write_enable(spi_device_handle_t spi) {
  uint8_t cmd = CMD_WRITE_ENABLE;
  return spi_nand_op(spi, &cmd, 1, NULL, 0);
}

// Geneirc implementations adapted from original uffs_spi_nand_read_page
int uffs_spi_nand_read_page_generic(struct uffs_DeviceSt *dev, uint32_t block,
                                    uint32_t page, uint8_t *data, int data_len,
                                    uint8_t *ecc, uint8_t *spare,
                                    int spare_len) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint32_t page_addr = block * priv->block_size + page;

  // 1. PAGE READ to Cache (0x13 + 3 byte addr)
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

  // 3. Check ECC Status
  int ecc_res = UFFS_FLASH_NO_ERR;
  int ecc_stat = (status & SR_ECC_MASK) >> 4;

  if (ecc_stat == 2) { // Uncorrectable
    ESP_LOGE(TAG, "ECC Uncorrectable Error at Blk %u Pg %u",
             (unsigned int)block, (unsigned int)page);
    return UFFS_FLASH_ECC_FAIL;
  } else if (ecc_stat == 1 || ecc_stat == 3) {
    ecc_res = UFFS_FLASH_ECC_OK; // Corrected
  }

  // 4. READ FROM CACHE (0x03 + 2 byte col addr + 1 dummy)
  // Read Data
  if (data && data_len > 0) {
    // Col Addr 0
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
    // Calculate Col Addr for spare
    uint16_t col = priv->page_size; // Start of spare
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

int uffs_spi_nand_write_page_generic(struct uffs_DeviceSt *dev, uint32_t block,
                                     uint32_t page, const uint8_t *data,
                                     int data_len, const uint8_t *spare,
                                     int spare_len) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint32_t page_addr = block * priv->block_size + page;

  // 1. Write Enable
  if (spi_nand_write_enable(priv->spi) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 2. Program Load (0x02 + 2 byte col addr) - Data

  // Load Data (Col 0)
  if (data && data_len > 0) {
    uint8_t cmd[3] = {CMD_PROGRAM_LOAD, 0, 0};

    spi_transaction_t t1 = {
        .length = 24, .tx_buffer = cmd, .flags = SPI_TRANS_CS_KEEP_ACTIVE};
    if (spi_device_transmit(priv->spi, &t1) != ESP_OK)
      return UFFS_FLASH_IO_ERR;

    spi_transaction_t t2 = {
        .length = (size_t)data_len * 8,
        .tx_buffer = data,
    };
    if (spi_device_transmit(priv->spi, &t2) != ESP_OK)
      return UFFS_FLASH_IO_ERR;
  }

  // Load Spare (Col PageSize)
  if (spare && spare_len > 0) {
    uint8_t cmd_code;
    if (data && data_len > 0) {
      cmd_code = 0x84; // Random Data Input (Modify existing buffer)
    } else {
      cmd_code = 0x02; // Program Load (Reset buffer)
    }

    uint16_t col = priv->page_size;
    uint8_t cmd[3] = {cmd_code, (col >> 8) & 0xFF, col & 0xFF};

    spi_transaction_t t1 = {
        .length = 24, .tx_buffer = cmd, .flags = SPI_TRANS_CS_KEEP_ACTIVE};
    if (spi_device_transmit(priv->spi, &t1) != ESP_OK)
      return UFFS_FLASH_IO_ERR;

    spi_transaction_t t2 = {
        .length = (size_t)spare_len * 8,
        .tx_buffer = spare,
    };
    if (spi_device_transmit(priv->spi, &t2) != ESP_OK)
      return UFFS_FLASH_IO_ERR;
  }

  // 3. Program Execute (0x10 + 3 byte Addr)
  uint8_t cmd_exec[4];
  cmd_exec[0] = CMD_PROGRAM_EXECUTE;
  cmd_exec[1] = (page_addr >> 16) & 0xFF;
  cmd_exec[2] = (page_addr >> 8) & 0xFF;
  cmd_exec[3] = page_addr & 0xFF;

  if (spi_nand_op(priv->spi, cmd_exec, 4, NULL, 0) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 4. Wait for Finish
  uint8_t status = 0;
  if (spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, &status) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  if (status & SR_P_FAIL) {
    ESP_LOGE(TAG, "Program Failed at Blk %u Pg %u (Stat: 0x%02X)",
             (unsigned int)block, (unsigned int)page, status);
    return UFFS_FLASH_BAD_BLK;
  }

  return UFFS_FLASH_NO_ERR;
}

int uffs_spi_nand_erase_block_generic(struct uffs_DeviceSt *dev,
                                      uint32_t block) {
  spi_nand_priv_t *priv = (spi_nand_priv_t *)dev->attr->_private;
  uint32_t page_addr = block * priv->block_size; // Row address is page index

  // 1. Write Enable
  if (spi_nand_write_enable(priv->spi) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 2. Block Erase (0xD8 + 3 byte Addr)
  uint8_t cmd_erase[4];
  cmd_erase[0] = CMD_BLOCK_ERASE;
  cmd_erase[1] = (page_addr >> 16) & 0xFF;
  cmd_erase[2] = (page_addr >> 8) & 0xFF;
  cmd_erase[3] = page_addr & 0xFF;

  if (spi_nand_op(priv->spi, cmd_erase, 4, NULL, 0) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  // 3. Wait
  uint8_t status = 0;
  if (spi_nand_wait_busy(priv->spi, NAND_TIMEOUT_MS, &status) != ESP_OK)
    return UFFS_FLASH_IO_ERR;

  if (status & SR_E_FAIL) {
    ESP_LOGE(TAG, "Erase Failed at Blk %u", (unsigned int)block);
    return UFFS_FLASH_BAD_BLK;
  }

  return UFFS_FLASH_NO_ERR;
}
