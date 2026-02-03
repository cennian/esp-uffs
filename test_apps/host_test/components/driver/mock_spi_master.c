#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "MockSPI";

// Mock Flash Parameters
#define MOCK_PAGE_SIZE 2048
#define MOCK_SPARE_SIZE 64
#define MOCK_PAGES_PER_BLOCK 64
#define MOCK_TOTAL_BLOCKS 1024
#define MOCK_CACHE_SIZE (MOCK_PAGE_SIZE + MOCK_SPARE_SIZE)

// Commands
#define CMD_RESET 0xFF
#define CMD_GET_FEATURE 0x0F
#define CMD_SET_FEATURE 0x1F
#define CMD_READ_ID 0x9F
#define CMD_PAGE_READ 0x13
#define CMD_READ_CACHE 0x03
#define CMD_WRITE_ENABLE 0x06
#define CMD_PROGRAM_LOAD 0x02
#define CMD_RANDOM_DATA_INPUT 0x84
#define CMD_PROGRAM_EXECUTE 0x10
#define CMD_BLOCK_ERASE 0xD8

typedef struct {
  uint8_t data[MOCK_PAGE_SIZE];
  uint8_t spare[MOCK_SPARE_SIZE];
  bool is_erased;
} mock_page_t;

static mock_page_t flash_mem[MOCK_TOTAL_BLOCKS][MOCK_PAGES_PER_BLOCK];
static uint8_t page_cache[MOCK_CACHE_SIZE];
static uint8_t status_reg = 0;
static bool write_enabled = false;
static int data_input_mode = 0; // 0: None, 1: Expecting Data
static uint16_t current_col_addr = 0;
uint8_t mock_mfr_id = 0xEF; // Default to Winbond

void mock_nand_reset(void) {
  memset(flash_mem, 0xFF, sizeof(flash_mem));
  for (int b = 0; b < MOCK_TOTAL_BLOCKS; b++) {
    for (int p = 0; p < MOCK_PAGES_PER_BLOCK; p++) {
      flash_mem[b][p].is_erased = true;
    }
  }
  memset(page_cache, 0xFF, sizeof(page_cache));
  status_reg = 0;
  write_enabled = false;
  data_input_mode = 0;
  mock_mfr_id = 0xEF;
}

esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc) {
  uint8_t *tx = (uint8_t *)trans_desc->tx_buffer;
  uint8_t *rx = (uint8_t *)trans_desc->rx_buffer;
  size_t tx_len = trans_desc->length / 8;
  size_t rx_len = trans_desc->rxlength / 8;

  if (!tx && !rx)
    return ESP_OK;

  // DEBUG TRACE
  if (tx && tx_len > 0) {
    if (!data_input_mode)
      ESP_LOGV(TAG, "Cmd 0x%02X Len %zu", tx[0], tx_len);
    else
      ESP_LOGV(TAG, "Data Load Len %zu", tx_len);
  } else {
    ESP_LOGV(TAG, "Transmit (No TX data or Data Phase)");
  }

  // Handle Data Input Phase
  if (data_input_mode && tx && tx_len > 0) {
    size_t available = MOCK_CACHE_SIZE - current_col_addr;
    size_t copy_len = (tx_len < available) ? tx_len : available;
    if (copy_len > 0) {
      memcpy(page_cache + current_col_addr, tx, copy_len);
      current_col_addr += copy_len;
    }

    // Data load completes the command sequence for programmed I/O usually
    // But some might send chunks. Assuming single chunk for now or manual
    // reset. esp_spi_nand.c sends data in one go.
    data_input_mode = 0;
    return ESP_OK;
  }

  // Handle Command Phase
  if (tx && tx_len > 0) {
    uint8_t cmd = tx[0];

    switch (cmd) {
    case CMD_RESET:
      memset(page_cache, 0xFF, sizeof(page_cache));
      status_reg = 0;
      write_enabled = false;
      break;

    case CMD_GET_FEATURE: // 0x0F + Addr
      if (tx_len >= 2 && tx[1] == 0xC0 && rx && rx_len > 0) {
        rx[0] = status_reg;
      }
      break;

    case CMD_READ_ID: // 0x9F + Dummy
      if (rx && rx_len >= 2) {
        rx[0] = mock_mfr_id; // dynamic
        rx[1] = 0xAA;        // Device
      }
      break;

    case CMD_WRITE_ENABLE:
      write_enabled = true;
      status_reg |= (1 << 1); // WEL bit
      break;

    case CMD_PAGE_READ: // 0x13 + 3 addr
      if (tx_len >= 4) {
        uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
        ESP_LOGV(TAG, "PAGE_READ Addr 0x%06X", addr);
        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        uint32_t page = addr % MOCK_PAGES_PER_BLOCK;

        if (block < MOCK_TOTAL_BLOCKS) {
          memcpy(page_cache, flash_mem[block][page].data, MOCK_PAGE_SIZE);
          memcpy(page_cache + MOCK_PAGE_SIZE, flash_mem[block][page].spare,
                 MOCK_SPARE_SIZE);
        } else {
          memset(page_cache, 0xFF, MOCK_CACHE_SIZE);
        }
      }
      break;

    case CMD_READ_CACHE: // 0x03 + 2 col + 1 dummy
      if (tx_len >= 4 && rx && rx_len > 0) {
        uint16_t col = (tx[1] << 8) | tx[2];
        if (col < MOCK_CACHE_SIZE) {
          // Fix: Avoid reading past cache end
          size_t to_read = (rx_len < (MOCK_CACHE_SIZE - col))
                               ? rx_len
                               : (MOCK_CACHE_SIZE - col);
          memcpy(rx, page_cache + col, to_read);
        }
      }
      break;

    case CMD_PROGRAM_LOAD: // 0x02 + 2 col
      if (tx_len >= 3) {
        current_col_addr = (tx[1] << 8) | tx[2];
        // Reset buffer if this command implies it
        memset(page_cache, 0xFF, MOCK_CACHE_SIZE);
        data_input_mode = 1;
      }
      break;

    case CMD_RANDOM_DATA_INPUT: // 0x84 + 2 col
      if (tx_len >= 3) {
        current_col_addr = (tx[1] << 8) | tx[2];
        data_input_mode = 1;
      }
      break;

    case CMD_PROGRAM_EXECUTE: // 0x10 + 3 addr
      if (write_enabled && tx_len >= 4) {
        uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
        ESP_LOGV(TAG, "PROGRAM_EXEC Addr 0x%06X", addr);
        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        uint32_t page = addr % MOCK_PAGES_PER_BLOCK;

        if (block < MOCK_TOTAL_BLOCKS) {
          // NAND programming checks: can only change 1 to 0
          for (int i = 0; i < MOCK_PAGE_SIZE; i++) {
            flash_mem[block][page].data[i] &= page_cache[i];
          }
          for (int i = 0; i < MOCK_SPARE_SIZE; i++) {
            flash_mem[block][page].spare[i] &= page_cache[MOCK_PAGE_SIZE + i];
          }
          flash_mem[block][page].is_erased = false;
        }
        write_enabled = false;
        status_reg &= ~(1 << 1);
      }
      break;

    case CMD_BLOCK_ERASE: // 0xD8 + 3 addr
      if (write_enabled && tx_len >= 4) {
        uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
        ESP_LOGV(TAG, "BLOCK_ERASE Addr 0x%06X", addr);
        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        if (block < MOCK_TOTAL_BLOCKS) {
          for (int p = 0; p < MOCK_PAGES_PER_BLOCK; p++) {
            memset(flash_mem[block][p].data, 0xFF, MOCK_PAGE_SIZE);
            memset(flash_mem[block][p].spare, 0xFF, MOCK_SPARE_SIZE);
            flash_mem[block][p].is_erased = true;
          }
        }
        write_enabled = false;
        status_reg &= ~(1 << 1);
      }
      break;

    default:
      break;
    }
  }

  return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,
                                      spi_transaction_t *trans_desc) {
  return spi_device_transmit(handle, trans_desc);
}
