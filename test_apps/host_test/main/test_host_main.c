#include "esp_log.h"
#include "esp_spi_nand.h"
#include "uffs/uffs.h"
#include "uffs/uffs_fd.h"
#include "uffs/uffs_mtb.h"
#include "uffs/uffs_os.h"
#include "unity.h"
#include <stdarg.h> // for va_list
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "test_main";

extern void mock_nand_reset(void); // Defined in mock_spi_master.c

#define PFX "TEST: "

static uffs_Device uffs_dev;
static uffs_MountTable mount_table[] = {{
                                            .dev = &uffs_dev,
                                            .start_block = 0,
                                            .end_block = 0,
                                            .mount = "/data/",
                                            .prev = NULL,
                                        },
                                        {.dev = NULL}};

static void debug_output(const char *msg) { ESP_LOGI("UFFS", "%s", msg); }
static void debug_vprintf(const char *fmt, va_list args) {
  esp_log_writev(ESP_LOG_INFO, "UFFS", fmt, args);
}
static struct uffs_DebugMsgOutputSt m_ops = {.output = debug_output,
                                             .vprintf = debug_vprintf};

void setUp(void) {
  ESP_LOGI(TAG, "[setUp] Resetting mock NAND...");
  mock_nand_reset();

  if (uffs_InitDebugMessageOutput(&m_ops, UFFS_MSG_NOISY) != 0) {
    ESP_LOGE(TAG, "[setUp] uffs_InitDebugMessageOutput Failed!");
  }
  uffs_Perror(UFFS_MSG_SERIOUS, "Debug Output Verified!");

  // Initialize global UFFS objects (pools, locks)
  if (uffs_InitFileSystemObjects() != 0) {
    ESP_LOGE(TAG, "[setUp] uffs_InitFileSystemObjects Failed!");
  }

  ESP_LOGI(TAG, "[setUp] Clearing uffs_dev...");
  memset(&uffs_dev, 0, sizeof(uffs_dev));

  ESP_LOGI(TAG, "[setUp] Initializing SPI NAND...");
  // Re-init device and mount
  esp_uffs_spi_nand_init(&uffs_dev, (spi_device_handle_t)0x1);

  // Update mount table to match device geometry
  if (uffs_dev.attr) {
    mount_table[0].end_block = uffs_dev.attr->total_blocks - 1;
  }

  ESP_LOGI(TAG, "[setUp] Registering Mount Table...");
  uffs_RegisterMountTable(mount_table);

  ESP_LOGI(TAG, "[setUp] Mounting /data...");
  // Attempt mount
  int ret = uffs_Mount("/data/");
  ESP_LOGI(TAG, "[setUp] Mount returned: %d", ret);

  if (ret < 0) {
    // failed, format
    ESP_LOGW(TAG, "[setUp] Mount failed, formatting...");
    if (uffs_format("/data/") != 0) {
      ESP_LOGE(TAG, "[setUp] Format failed!");
    }
    ESP_LOGI(TAG, "[setUp] Mounting /data again...");
    ret = uffs_Mount("/data/");
    ESP_LOGI(TAG, "[setUp] Remount returned: %d", ret);
  }
}

void tearDown(void) {
  ESP_LOGI(TAG, "[tearDown] Unmounting...");
  uffs_UnMount("/data/");
  uffs_ReleaseFileSystemObjects();
}

TEST_CASE("uffs basic functional test", "[uffs][functional]") {
  const char *test_file = "/data/hello.txt";
  const char *content = "Hello World, this is UFFS on Host!";
  char buf[64] = {0};

  // Write
  int fd = uffs_open(test_file, UO_CREATE | UO_TRUNC | UO_WRONLY, 0);
  if (fd < 0) {
    TEST_FAIL_MESSAGE("Failed to open file for writing");
  }
  int written = uffs_write(fd, content, strlen(content));
  TEST_ASSERT_EQUAL(strlen(content), written);
  uffs_close(fd);

  // Read
  fd = uffs_open(test_file, UO_RDONLY, 0);
  if (fd < 0) {
    TEST_FAIL_MESSAGE("Failed to open file for reading");
  }
  int read_len = uffs_read(fd, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(strlen(content), read_len);
  TEST_ASSERT_EQUAL_STRING(content, buf);
  uffs_close(fd);

  // Append
  fd = uffs_open(test_file, UO_APPEND | UO_WRONLY, 0);
  if (fd < 0) {
    TEST_FAIL_MESSAGE("Failed to open for append");
  }
  uffs_write(fd, " Append", 7);
  uffs_close(fd);

  // Verify Append
  fd = uffs_open(test_file, UO_RDONLY, 0);
  int total_len = uffs_read(fd, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(strlen(content) + 7, total_len);
  uffs_close(fd);

  // Delete
  TEST_ASSERT_EQUAL(0, uffs_remove(test_file));
  TEST_ASSERT_TRUE_MESSAGE(uffs_open(test_file, UO_RDONLY, 0) < 0,
                           "File should be deleted");
}

TEST_CASE("uffs stress test - many files", "[uffs][stress]") {
  char filename[32];
  const int FILE_COUNT = 20; // Reduce for speed if needed

  ESP_LOGI(TAG, "Creating %d files...", FILE_COUNT);
  for (int i = 0; i < FILE_COUNT; i++) {
    sprintf(filename, "/data/f_%03d.txt", i);
    int fd = uffs_open(filename, UO_CREATE | UO_WRONLY, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
    uffs_write(fd, filename, strlen(filename)); // Write filename as content
    uffs_close(fd);
  }

  // Verify
  ESP_LOGI(TAG, "Verifying %d files...", FILE_COUNT);
  for (int i = 0; i < FILE_COUNT; i++) {
    sprintf(filename, "/data/f_%03d.txt", i);
    int fd = uffs_open(filename, UO_RDONLY, 0);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
    char buf[32] = {0};
    uffs_read(fd, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(filename, buf);
    uffs_close(fd);
  }
}

TEST_CASE("uffs stress test - large file write", "[uffs][stress]") {
  const char *filename = "/data/large.bin";
  const int SIZE = 128 * 1024; // 128KB - reduced slightly for small mock
  char *buf = malloc(SIZE);
  TEST_ASSERT_NOT_NULL(buf);

  // Fill buffer
  for (int i = 0; i < SIZE; i++)
    buf[i] = (char)(i & 0xFF);

  // Write time
  int fd = uffs_open(filename, UO_CREATE | UO_TRUNC | UO_WRONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

  struct timeval start, end;
  gettimeofday(&start, NULL);
  int written = uffs_write(fd, buf, SIZE);
  gettimeofday(&end, NULL);

  TEST_ASSERT_EQUAL(SIZE, written);
  uffs_close(fd);

  double elapsed =
      (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
  ESP_LOGI(TAG, "Wrote %d bytes in %.3f s (%.2f KB/s)", SIZE, elapsed,
           (SIZE / 1024.0) / elapsed);

  // Verify
  char *read_buf = malloc(SIZE);
  TEST_ASSERT_NOT_NULL(read_buf);

  fd = uffs_open(filename, UO_RDONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

  int read_len = uffs_read(fd, read_buf, SIZE);
  TEST_ASSERT_EQUAL(SIZE, read_len);
  TEST_ASSERT_EQUAL_INT8_ARRAY(buf, read_buf, SIZE);
  uffs_close(fd);

  free(buf);
  free(read_buf);
}

TEST_CASE("uffs bandwidth test", "[uffs][bandwidth]") {
  // Write 1MB file (flash is small)
  const char *filename = "/data/bw_test.bin";
  const int CHUNK_SIZE = 4096;
  const int TOTAL_SIZE = 1024 * 1024;
  char *chunk = malloc(CHUNK_SIZE);
  TEST_ASSERT_NOT_NULL(chunk);
  memset(chunk, 0xAB, CHUNK_SIZE);

  int fd = uffs_open(filename, UO_CREATE | UO_TRUNC | UO_WRONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

  struct timeval start, end;
  gettimeofday(&start, NULL);

  for (int i = 0; i < TOTAL_SIZE / CHUNK_SIZE; i++) {
    int w = uffs_write(fd, chunk, CHUNK_SIZE);
    if (w != CHUNK_SIZE) {
      ESP_LOGE(TAG, "Write failed at chunk %d: %d", i, w);
      TEST_FAIL_MESSAGE("Write failed during bandwidth test");
    }
  }

  gettimeofday(&end, NULL);
  uffs_close(fd);

  double elapsed =
      (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
  ESP_LOGI(TAG, "BW Write: %.2f MB/s",
           (TOTAL_SIZE / 1024.0 / 1024.0) / elapsed);

  // Read
  fd = uffs_open(filename, UO_RDONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

  gettimeofday(&start, NULL);
  while (uffs_read(fd, chunk, CHUNK_SIZE) > 0)
    ;
  gettimeofday(&end, NULL);
  uffs_close(fd);

  elapsed =
      (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
  ESP_LOGI(TAG, "BW Read: %.2f MB/s", (TOTAL_SIZE / 1024.0 / 1024.0) / elapsed);

  free(chunk);
}

// Test initialization for all supported vendors
extern uint8_t mock_mfr_id; // From mock_spi_master.c

TEST_CASE("api init all vendors", "[uffs][init]") {
  struct {
    uint8_t id;
    const char *name;
  } vendors[] = {
      {0xEF, "Winbond"},  {0xC8, "GigaDevice"}, {0x2C, "Micron"},
      {0x52, "Alliance"}, {0xBA, "Zetta"},      {0x0B, "XTX"},
      {0xFF, "Generic"} // Failover check
  };

  for (int i = 0; i < sizeof(vendors) / sizeof(vendors[0]); i++) {
    ESP_LOGI(TAG, "Testing init for %s (0x%02X)...", vendors[i].name,
             vendors[i].id);
    mock_mfr_id = vendors[i].id;
    mock_nand_reset(); // Clean slate

    uffs_Device dev_tmp;
    memset(&dev_tmp, 0, sizeof(dev_tmp));

    esp_err_t ret = esp_uffs_spi_nand_init(&dev_tmp, (spi_device_handle_t)0x1);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(dev_tmp.attr);
    TEST_ASSERT_NOT_NULL(dev_tmp.ops);

    // Simple verification that attr is populated
    TEST_ASSERT_GREATER_THAN(0, dev_tmp.attr->total_blocks);

    // Cleanup if needed (free memory allocated by init)
    // Note: Real UFFS would need careful shutdown, here we rely on
    // mock_nand_reset and just freeing what init allocated:
    if (dev_tmp.attr) {
      if (dev_tmp.attr->_private)
        free(dev_tmp.attr->_private);
      free(dev_tmp.attr);
    }
    if (dev_tmp.ops)
      free(dev_tmp.ops);
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Running UFFS Comprehensive Host Test Suite...");
  unity_run_menu();
}
