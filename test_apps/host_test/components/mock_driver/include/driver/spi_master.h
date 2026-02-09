#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spi_device_t *spi_device_handle_t;

#define SPI_TRANS_CS_KEEP_ACTIVE (1 << 0)

#define SPI_TRANS_USE_RXDATA (1 << 2)
#define SPI_TRANS_USE_TXDATA (1 << 3)

typedef struct {
  uint32_t flags;
  uint16_t cmd;
  uint64_t addr;
  uint32_t length;
  uint32_t rxlength;
  void *user;
  union {
    const void *tx_buffer;
    uint8_t tx_data[4];
  };
  union {
    void *rx_buffer;
    uint8_t rx_data[4];
  };
} spi_transaction_t;

esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,
                                      spi_transaction_t *trans_desc);

#ifdef __cplusplus
}
#endif
