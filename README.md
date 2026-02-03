# ESP-UFFS

[![Component Registry](https://components.espressif.com/components/espressif/esp-uffs/badge.svg)](https://components.espressif.com/components/espressif/esp-uffs)

**ESP-UFFS** is a port of the Ultra-low-cost Flash File System (UFFS) for the ESP-IDF framework. It is explicitly designed for **raw NAND flash** (SPI NAND), providing robust bad block management, wear leveling, and power-fail safety that standard file systems (like FATFS) struggle to deliver on raw flash media.

## Features

*   **Raw NAND Support**: Optimized for SPI NAND chips with internal ECC or software ECC.
*   **Bad Block Management**: Automatically handles factory bad blocks and runtime block failures.
*   **Wear Leveling**: Distributes writes evenly across the flash to extend lifespan.
*   **Power-Fail Safety**: Designed to maintain filesystem consistency even after unexpected power loss.
*   **Multi-Vendor Support**: Out-of-the-box support for major SPI NAND manufacturers.
*   **Low Memory Footprint**: Tunable memory usage suitable for embedded systems.

## Architecture

The component is structured into three layers:

1.  **Core Engine** (`src/`): The platform-independent UFFS library that handles filesystem logic (trees, journaling, gc).
2.  **Port Layer** (`port/uffs_port.c`): Adapts UFFS to ESP-IDF OS primitives (FreeRTOS mutexes, `esp_timer`, heap allocator).
3.  **Driver Glue** (`port/esp_spi_nand.c`): Bridges the UFFS hardware operations (`uffs_FlashOps`) to the ESP-IDF `spi_master` driver.

### Supported Hardware

This component uses the standard `spi_master` driver, making it compatible with all ESP32 variants that support SPI:

*   **Controllers**: ESP32, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C6, ESP32-H2, ESP32-P4.
*   **Flash Chips**:
    *   **Winbond** (W25N series)
    *   **GigaDevice** (GD5F series)
    *   **Micron** (MT29F series)
    *   **Alliance Memory** (AS5F series)
    *   **Zetta** (ZD25 series)
    *   **XTX** (XT26 series)

> **Note**: If your specific chip ID is not recognized, the driver falls back to a **Generic Driver** which works for most standard ONFI-compatible SPI NANDs (assuming Read/Write/Erase opcodes match standards).

## Usage

### 1. Installation

**Note**: This component is not yet registered in the Espressif Component Registry. Please install it manually.

**Option A: Clone as Submodule** (Recommended)
```bash
cd your_project/components
git submodule add https://github.com/your_username/esp-uffs.git uffs
```

**Option B: Copy Source**
Copy the entire `uffs` folder into your project's `components/` directory.

### 2. Initialization

```c
#include "esp_spi_nand.h"
#include "uffs/uffs.h"

void app_main(void) {
    // 1. Initialize SPI Bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_MOSI_PIN,
        .miso_io_num = CONFIG_MISO_PIN,
        .sclk_io_num = CONFIG_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096, // Must be > page + spare size
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Add SPI Device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz
        .mode = 0,
        .spics_io_num = CONFIG_CS_PIN,
        .queue_size = 7,
    };
    spi_device_handle_t spi_handle;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle));

    // 3. Initialize UFFS Device & Driver
    static uffs_Device uffs_dev;
    ESP_ERROR_CHECK(esp_uffs_spi_nand_init(&uffs_dev, spi_handle));

    // 4. Mount Filesystem
    uffs_Mount("/data");

    // 5. Use Filesystem (POSIX-like or Native)
    // Note: To use strict POSIX (fopen), you need VFS registration (feature pending).
    // Currently, uffs_open / uffs_write / uffs_read are available.
    
    int fd = uffs_open("/data/test.txt", UFFS_CREAT | UFFS_RDWR, 0);
    if (fd > 0) {
        uffs_write(fd, "Hello World", 11);
        uffs_close(fd);
    }
}
```

### Dos and Don'ts

*   **DO** ensure your SPI bus `max_transfer_sz` is at least the size of a NAND page + spare (typically ~2112 bytes).
*   **DO** use the `esp_uffs_spi_nand_init` helper; it automatically detects the flash vendor and loads the correct ECC/Block-locking logic.
*   **DON'T** share the SPI bus with high-frequency interrupt-driven devices (like screens) without careful transaction management, as NAND operations can be blocking.
*   **DON'T** format the chip unnecessarily; UFFS will attempt to mount existing data. Explicitly call `uffs_Format` only if mounting fails or you want a clean slate.

## Benchmarks

Hardware performance varies by SPI frequency, caching, and chip model. The following are theoretical limits verified on a Host Mock environment (simulating perfect SPI bus):

| Operation | Throughput (MB/s)* |
|-----------|--------------------|
| **Write** | ~213 MB/s          |
| **Read**  | ~897 MB/s          |

*\*Note: Real-world ESP32 SPI throughput will be lower (typically 1-5 MB/s depending on wiring and frequency) due to bus overhead.*

## Testing

### Running Host Tests (Linux)

You can run the full test suite on your host machine (Linux) without an ESP32. This uses a mock SPI driver (`mock_spi_master.c`) to simulate NAND flash in RAM.

1.  Navigate to the test app:
    ```bash
    cd components/uffs/test_apps/host_test
    ```
2.  Set up the IDF environment:
    ```bash
    . $HOME/esp/esp-idf/export.sh
    ```
3.  Build and Run:
    ```bash
    idf.py build
    ./build/test_host_uffs.elf
    ```
4.  Select a test from the menu (e.g., `1` for initialization test, `2` for bandwidth).

### Running on Target (ESP32)

To test on real hardware:

1.  Create a project using the component.
2.  Connect your SPI NAND flash to the specified pins.
3.  Flash and monitor:
    ```bash
    idf.py flash monitor
    ```

## API Reference

### Component Init
*   `esp_err_t esp_uffs_spi_nand_init(uffs_Device *dev, spi_device_handle_t spi_handle)`: Initializes the UFFS device structure and detects the connected flash chip.

### Mount Operations (uffs/uffs_mtb.h)
*   `int uffs_Mount(const char *mount_point)`: Mounts the filesystem at the specified path (e.g., "/data").
*   `int uffs_UnMount(const char *mount_point)`: Unmounts the filesystem.
*   `int uffs_format(const char *mount_point)`: Formats the partition. **Warning: All data will be lost.**

### File Operations (uffs/uffs_fd.h)
*   `int uffs_open(const char *name, int oflag, ...)`: Open or create a file.
    *   Flags: `UFFS_RDONLY`, `UFFS_WRONLY`, `UFFS_RDWR`, `UFFS_CREAT`, `UFFS_TRUNC`, `UFFS_APPEND`.
*   `int uffs_write(int fd, const void *data, int len)`: Write data to an open file.
*   `int uffs_read(int fd, void *data, int len)`: Read data from an open file.
*   `int uffs_close(int fd)`: Close a file descriptor.
*   `long uffs_seek(int fd, long offset, int origin)`: Move read/write pointer.
*   `int uffs_remove(const char *name)`: Delete a file.
*   `int uffs_mkdir(const char *name)`: Create a directory.
*   `int uffs_rmdir(const char *name)`: Delete a directory.

## Future Improvements

We welcome contributions! Key areas for improvement:

- [ ] **VFS Integration**: Register UFFS with ESP-IDF Virtual File System (`esp_vfs_register`) to support standard `fopen`/`fprintf`.
- [ ] **DMA Optimization**: Better use of SPI DMA for large transfers.
- [ ] **QPI/OPI Support**: Add support for Quad SPI mode for higher throughput.
- [ ] **More Vendors**: Add specific drivers for Toshiba/Kioxia or Macronix if they differ from ONFI.

## Acknowledgements

This project is a port of the excellent **UFFS (Ultra-low-cost Flash File System)** library.
*   **Original UFFS Repository**: [https://github.com/rickyzheng/uffs](https://github.com/rickyzheng/uffs)
*   **Author**: Ricky Zheng

We appreciate the work done by the UFFS community in creating a robust filesystem for raw flash.

## License

This component is provided under a **mixed license**:

1.  **UFFS Core** (`src/`, `include/uffs/`):
    *   Copyright (C) 2005-2009 Ricky Zheng
    *   Licensed under **LGPL-2.0-or-later** (GNU Library General Public License).
    *   *Note*: The library includes a special exception allowing linking without infecting the application license.

2.  **ESP-IDF Port** (`port/`, `include/esp_spi_nand.h`):
    *   Copyright (C) 2024 Ihtesham Ullah
    *   Licensed under **Apache-2.0**.
