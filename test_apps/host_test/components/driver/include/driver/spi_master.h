#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spi_device_t *spi_device_handle_t;

#define SPI_TRANS_CS_KEEP_ACTIVE (1 << 0)

typedef struct {
  uint32_t flags;
  uint32_t length;
  uint32_t rxlength;
  const void *tx_buffer;
  void *rx_buffer;
} spi_transaction_t;

esp_err_t spi_device_transmit(spi_device_handle_t handle,
                              spi_transaction_t *trans_desc);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,
                                      spi_transaction_t *trans_desc);

#ifdef __cplusplus
}
#endif
