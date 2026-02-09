#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MOCK_SPI"

// MOCK_TOTAL_BLOCKS is now a variable, checking PSRAM at runtime
static int mock_total_blocks = 128; // Default safe value
#define TOTAL_BLOCKS mock_total_blocks

// Mock Flash Parameters
#define MOCK_PAGE_SIZE 2048
#define MOCK_SPARE_SIZE 64
#define MOCK_PAGES_PER_BLOCK 64
#define MOCK_TOTAL_BLOCKS mock_total_blocks
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

// Sparse array of Block pointers.
// flash_mem[block] is a pointer to an array of page pointers.
// Will be allocated at runtime based on PSRAM availability
static mock_page_t ***flash_mem = NULL;

static uint8_t page_cache[MOCK_CACHE_SIZE];
static uint8_t status_reg = 0;
static bool write_enabled = false;
static int data_input_mode = 0; // 0: None, 1: Expecting Data
static uint16_t current_col_addr = 0;
uint8_t mock_mfr_id = 0xEF; // Default to Winbond

// Helper to init memory if not already done
static void mock_spi_init_mem(void) {
  if (flash_mem)
    return;

  // Check for PSRAM availability (1MB threshold arbitrary but safe)
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 1024 * 1024) {
    mock_total_blocks = 1024; // 128MB Flash
    ESP_LOGI(TAG, "PSRAM Detected: Setting Mock Flash to %d Blocks (128MB)",
             mock_total_blocks);
  } else {
    mock_total_blocks = 128; // 16MB Flash
    ESP_LOGI(TAG, "No PSRAM: Setting Mock Flash to %d Blocks (16MB)",
             mock_total_blocks);
  }

  flash_mem = calloc(mock_total_blocks, sizeof(mock_page_t **));
  if (!flash_mem) {
    ESP_LOGE(TAG, "Critical: Failed to allocate mock flash block table!");
    abort();
  }
}

void mock_nand_reset(void) {
  mock_spi_init_mem(); // Ensure memory is initialized

  for (int b = 0; b < MOCK_TOTAL_BLOCKS; b++) {
    if (flash_mem[b]) {
      for (int p = 0; p < MOCK_PAGES_PER_BLOCK; p++) {
        if (flash_mem[b][p]) {
          free(flash_mem[b][p]);
          flash_mem[b][p] = NULL;
        }
      }
      free(flash_mem[b]);
      flash_mem[b] = NULL;
    }
  }
  memset(page_cache, 0xFF, sizeof(page_cache));
  status_reg = 0;
  write_enabled = false;
  data_input_mode = 0;
  mock_mfr_id = 0xEF;
}

static mock_page_t *get_page_alloc(int block, int page) {
  if (block >= MOCK_TOTAL_BLOCKS || page >= MOCK_PAGES_PER_BLOCK)
    return NULL;

  // Allocate Block Table if missing
  if (flash_mem[block] == NULL) {
    flash_mem[block] = calloc(MOCK_PAGES_PER_BLOCK, sizeof(mock_page_t *));
    if (!flash_mem[block]) {
      ESP_LOGE(TAG, "Failed to allocate block table for B%d", block);
      return NULL;
    }
  }

  // Allocate Page if missing
  if (flash_mem[block][page] == NULL) {
    flash_mem[block][page] = malloc(sizeof(mock_page_t));
    if (flash_mem[block][page]) {
      memset(flash_mem[block][page]->data, 0xFF, MOCK_PAGE_SIZE);
      memset(flash_mem[block][page]->spare, 0xFF, MOCK_SPARE_SIZE);
      flash_mem[block][page]->is_erased = true;
    }
  }
  return flash_mem[block][page];
}

esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc) {
  uint8_t *tx;
  uint8_t *rx;

  if (trans_desc->flags & SPI_TRANS_USE_TXDATA) {
    tx = (uint8_t *)trans_desc->tx_data;
  } else {
    tx = (uint8_t *)trans_desc->tx_buffer;
  }

  if (trans_desc->flags & SPI_TRANS_USE_RXDATA) {
    rx = (uint8_t *)trans_desc->rx_data;
  } else {
    rx = (uint8_t *)trans_desc->rx_buffer;
  }

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
    data_input_mode = 0;
    return ESP_OK;
  }

  // Handle Command Phase
  if (tx && tx_len > 0) {
    uint8_t cmd = tx[0];

    switch (cmd) {
    case CMD_RESET:
      mock_nand_reset(); // Full reset
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
        ESP_LOGV(TAG, "PAGE_READ Addr 0x%06" PRIx32, addr);
        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        uint32_t page = addr % MOCK_PAGES_PER_BLOCK;

        if (block < MOCK_TOTAL_BLOCKS) {
          // Check if block table exists
          if (flash_mem[block]) {
            mock_page_t *p = flash_mem[block][page];
            if (p) {
              memcpy(page_cache, p->data, MOCK_PAGE_SIZE);
              memcpy(page_cache + MOCK_PAGE_SIZE, p->spare, MOCK_SPARE_SIZE);
            } else {
              memset(page_cache, 0xFF, MOCK_CACHE_SIZE);
            }
          } else {
            memset(page_cache, 0xFF, MOCK_CACHE_SIZE);
          }
        } else {
          memset(page_cache, 0xFF, MOCK_CACHE_SIZE);
        }
      }
      break;

    case CMD_READ_CACHE: // 0x03 + 2 col + 1 dummy
      if (tx_len >= 4 && rx && rx_len > 0) {
        uint16_t col = (tx[1] << 8) | tx[2];
        if (col < MOCK_CACHE_SIZE) {
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
        ESP_LOGV(TAG, "PROGRAM_EXEC Addr 0x%06" PRIx32, addr);
        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        uint32_t page = addr % MOCK_PAGES_PER_BLOCK;

        if (block < MOCK_TOTAL_BLOCKS) {
          mock_page_t *p = get_page_alloc(block, page);
          if (p) {
            // NAND programming checks: can only change 1 to 0
            for (int i = 0; i < MOCK_PAGE_SIZE; i++) {
              p->data[i] &= page_cache[i];
            }
            for (int i = 0; i < MOCK_SPARE_SIZE; i++) {
              p->spare[i] &= page_cache[MOCK_PAGE_SIZE + i];
            }
            p->is_erased = false;
          } else {
            ESP_LOGE(TAG,
                     "Mock Flash Full! Alloc failed for B%" PRIu32 ":P%" PRIu32,
                     block, page);
            status_reg |= (1 << 2); // Set P_FAIL (Bit 2)
          }
        } else {
          ESP_LOGE(TAG, "Access out of bounds: B%" PRIu32, block);
          status_reg |= (1 << 2); // Set P_FAIL (Bit 2)
        }
        write_enabled = false;
        status_reg &= ~(1 << 1);
      }
      break;

    case CMD_BLOCK_ERASE: // 0xD8 + 3 addr
      if (write_enabled && tx_len >= 4) {
        uint32_t addr = (tx[1] << 16) | (tx[2] << 8) | tx[3];
        ESP_LOGV(TAG, "BLOCK_ERASE Addr 0x%06" PRIx32, addr);

        uint32_t block = addr / MOCK_PAGES_PER_BLOCK;
        if (block < MOCK_TOTAL_BLOCKS) {
          // Only erase if block table allocated
          if (flash_mem[block]) {
            for (int p = 0; p < MOCK_PAGES_PER_BLOCK; p++) {
              // Free memory efficiently
              if (flash_mem[block][p]) {
                free(flash_mem[block][p]);
                flash_mem[block][p] = NULL;
              }
            }
            // Optional: We could free the block table here too if we wanted to
            // be super aggressive, but keeping it is fine as it's small (256
            // bytes per block). Let's keep it to avoid re-alloc churn on reuse.
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
