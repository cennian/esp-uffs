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

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "uffs/uffs_os.h"
#include "uffs/uffs_public.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

static const char *TAG = "uffs";

static void uffs_debug_output(const char *msg) { ESP_LOGD(TAG, "%s", msg); }

static void uffs_debug_vprintf(const char *fmt, va_list args) {
  esp_log_writev(ESP_LOG_DEBUG, TAG, fmt, args);
}

void uffs_SetupDebugOutput(void) {
  struct uffs_DebugMsgOutputSt output;
  output.output = uffs_debug_output;
  output.vprintf = uffs_debug_vprintf;

  uffs_InitDebugMessageOutput(&output, UFFS_MSG_NORMAL);
}

// OS Specific Functions

int uffs_SemCreate(OSSEM *sem) {
  SemaphoreHandle_t s = xSemaphoreCreateMutex();
  if (s == NULL) {
    ESP_LOGE(TAG, "[Port] SemCreate Failed!");
    return -1;
  }
  *sem = (OSSEM)s;
  return 0;
}

int uffs_SemWait(OSSEM sem) {
  SemaphoreHandle_t s = (SemaphoreHandle_t)sem;
  if (xSemaphoreTake(s, portMAX_DELAY) == pdTRUE) {
    return 0;
  }
  return -1;
}

int uffs_SemSignal(OSSEM sem) {
  SemaphoreHandle_t s = (SemaphoreHandle_t)sem;
  if (xSemaphoreGive(s) == pdTRUE) {
    return 0;
  }
  return -1; // Silent fail log?
}

int uffs_SemDelete(OSSEM *sem) {
  if (sem && *sem) {
    vSemaphoreDelete((SemaphoreHandle_t)(*sem));
    *sem = NULL;
  }
  return 0;
}

int uffs_OSGetTaskId(void) {
  void *handle = xTaskGetCurrentTaskHandle();
  // fprintf(stderr, "[Port] TaskID ptr=%p cast=%d\n", handle,
  // (int)(intptr_t)handle);
  return (int)(intptr_t)handle;
}

unsigned int uffs_GetCurDateTime(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (unsigned int)tv.tv_sec;
}
