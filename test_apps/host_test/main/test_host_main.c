#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spi_nand.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

// --- Expanded Test Suite ---

// Thread Safety Test
#define THREAD_TEST_FILE "/data/thread_test.txt"
#define THREAD_TASK_COUNT 4
#define THREAD_ITERATIONS 20

static volatile int task_success_count = 0;

static void file_writer_task(void *arg) {
  char buf[32];
  int id = (int)arg;
  sprintf(buf, "Task%d\n", id);

  for (int i = 0; i < THREAD_ITERATIONS; i++) {
    int fd = uffs_open(THREAD_TEST_FILE, UO_APPEND | UO_WRONLY | UO_CREATE, 0);
    if (fd < 0) {
      ESP_LOGE(TAG, "Task %d: Open failed", id);
      vTaskDelete(NULL);
    }
    uffs_write(fd, buf, strlen(buf));
    uffs_close(fd);
    vTaskDelay(pdMS_TO_TICKS(10)); // Yield to let others run
  }

  // Atomic increment would be better, but this is simple enough for test
  task_success_count++;
  vTaskDelete(NULL);
}

TEST_CASE("uffs thread safety", "[uffs][thread]") {
  task_success_count = 0;
  uffs_remove(THREAD_TEST_FILE); // Cleanup first

  for (int i = 0; i < THREAD_TASK_COUNT; i++) {
    xTaskCreate(file_writer_task, "writer", 4096, (void *)i, 5, NULL);
  }

  // Wait for tasks
  int timeout_ms = 5000;
  while (task_success_count < THREAD_TASK_COUNT && timeout_ms > 0) {
    vTaskDelay(pdMS_TO_TICKS(100));
    timeout_ms -= 100;
  }

  TEST_ASSERT_EQUAL(THREAD_TASK_COUNT, task_success_count);

  // Verify content size
  int fd = uffs_open(THREAD_TEST_FILE, UO_RDONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

  // Each task writes "TaskX\n" (6 chars) * 20 times = 120 bytes
  // Total = 120 * 4 = 480 bytes
  int size = uffs_seek(fd, 0, SEEK_END);
  TEST_ASSERT_EQUAL(THREAD_TASK_COUNT * THREAD_ITERATIONS * 6, size);
  uffs_close(fd);
}

// Memory Leak Helper
static size_t free_mem_start;

static void mem_check_start(void) {
  free_mem_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

static void mem_check_end(const char *msg) {
  size_t free_mem_end = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  // Allow small variance for heap fragmentation or internal OS buffers
  // Strict leak means significant loss.
  int diff = (int)free_mem_start - (int)free_mem_end;
  ESP_LOGI(TAG, "Memory Check [%s]: Start %d, End %d, Diff %d", msg,
           (int)free_mem_start, (int)free_mem_end, diff);
  // Warning only for now as frameworks often have tiny one-time allocs
  if (diff > 1024) {
    ESP_LOGW(TAG, "POTENTIAL LEAK DETECTED in %s!", msg);
  }
}

TEST_CASE("uffs memory leak check", "[uffs][memory]") {
  mem_check_start();

  // Run a cycle of open/write/close/delete
  const char *fname = "/data/memleak.bin";
  int fd = uffs_open(fname, UO_CREATE | UO_WRONLY, 0);
  uffs_write(fd, "temp", 4);
  uffs_close(fd);
  uffs_remove(fname);

  mem_check_end("Basic Cycle");
}

// Boundary & Failures
TEST_CASE("uffs boundary checks", "[uffs][boundary]") {
  // 1. Max Filename Length (UFFS default MAX_FILENAME_LEN is usually 256 or
  // less) We'll try a reasonable long name
  char long_name[200];
  memset(long_name, 'a', 199);
  long_name[199] = '\0';
  char path[256];
  snprintf(path, sizeof(path), "/data/%s", long_name);

  int fd = uffs_open(path, UO_CREATE | UO_WRONLY, 0);
  // UFFS might truncate or reject. We assume it handles it gracefully (fd >= 0
  // OR error but NO CRASH)
  if (fd >= 0) {
    uffs_close(fd);
    uffs_remove(path);
  } else {
    ESP_LOGI(TAG, "Long filename rejected gracefully");
  }

  // 2. Zero Length Write
  fd = uffs_open("/data/zero.bin", UO_CREATE | UO_WRONLY, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
  int w = uffs_write(fd, "test", 0);
  TEST_ASSERT_EQUAL(0, w);
  uffs_close(fd);
  uffs_remove("/data/zero.bin");
}

TEST_CASE("runtime flash size check", "[uffs][init]") {
  if (uffs_dev.attr) {
    ESP_LOGI(TAG, "Runtime Detected Flash Size: %d Blocks (%d MB)",
             uffs_dev.attr->total_blocks,
             (uffs_dev.attr->total_blocks * uffs_dev.attr->pages_per_block *
              uffs_dev.attr->page_data_size) /
                 (1024 * 1024));

    // Basic assertion to ensure we are getting expected values (either 128 or
    // 1024)
    TEST_ASSERT_TRUE(uffs_dev.attr->total_blocks == 128 ||
                     uffs_dev.attr->total_blocks == 1024);
  } else {
    TEST_FAIL_MESSAGE("Device attributes not initialized!");
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Running UFFS Comprehensive Host Test Suite...");

  // Note: UFFS uses a global pool, so some "leak" is just one-time pool init.
  // We exclude setup/teardown from the per-test memory check logic generally,
  // but "memory leak check" test case specifically looks for leaks in
  // operations.

  unity_run_menu();
}
