#include <thread>
#include <chrono>
#include <cstring>
#include <esp_pthread.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime_profiler.h"

#if defined(PROFILING)
constexpr const char *LOG_TAG = "rp";
constexpr int MAX_TASKS_SUPPORTED = 30;
static TaskStatus_t currentTaskStats[MAX_TASKS_SUPPORTED];
static TaskStatus_t previousTaskStats[MAX_TASKS_SUPPORTED];
static uint32_t previousTotalRunTime = 0;

uint32_t getPreviousTaskRuntimeCounter(const char *taskName, int core_id)
{
    for (int i = 0; i < MAX_TASKS_SUPPORTED; i++)
    {
        if (previousTaskStats[i].pcTaskName && previousTaskStats[i].xCoreID == core_id && strncmp(taskName, previousTaskStats[i].pcTaskName, strlen(taskName)) == 0)
        {
            return previousTaskStats[i].ulRunTimeCounter;
        }
    }
    return 0;
}

void GetRunTimeStats(char *pcWriteBuffer)
{
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime, ulStatsAsPercentage, elapsedRunTime;

    long curtime = esp_timer_get_time() / 1000;

    // Make sure the write buffer does not contain a string.
    *pcWriteBuffer = 0x00;

    // Take a snapshot of the number of tasks in case it changes while this
    // function is executing.
    uxArraySize = uxTaskGetNumberOfTasks();

    // Generate raw status information about each task.
    //  uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime );
    uxArraySize = uxTaskGetSystemState(currentTaskStats, uxArraySize, &ulTotalRunTime);

    // For percentage calculations.
    ulTotalRunTime /= 100UL;
    elapsedRunTime = ulTotalRunTime - previousTotalRunTime;
    previousTotalRunTime = ulTotalRunTime;

    // Avoid divide by zero errors.
    if (elapsedRunTime > 0)
    {
        // For each populated position in the pxTaskStatusArray array,
        // format the raw data as human readable ASCII data
        pcWriteBuffer += strlen(pcWriteBuffer);
        for (x = 0; x < uxArraySize; x++)
        {
            // What percentage of the total run time has the task used?
            // This will always be rounded down to the nearest integer.
            // ulTotalRunTimeDiv100 has already been divided by 100.

            uint32_t taskRunTimeSinceLastMeasurement = currentTaskStats[x].ulRunTimeCounter - getPreviousTaskRuntimeCounter(currentTaskStats[x].pcTaskName, currentTaskStats[x].xCoreID);
            ulStatsAsPercentage = taskRunTimeSinceLastMeasurement / elapsedRunTime;

            sprintf(pcWriteBuffer, "rp-t,%lu,%s,%lu,%lu,%d,%lu\n", curtime, currentTaskStats[x].pcTaskName, (unsigned long)currentTaskStats[x].ulRunTimeCounter, (unsigned long)ulStatsAsPercentage, currentTaskStats[x].xCoreID, currentTaskStats[x].usStackHighWaterMark);
            pcWriteBuffer += strlen((char *)pcWriteBuffer);
        }

        // Update previous values
        memset(previousTaskStats, 0, sizeof(previousTaskStats));
        for (int i = 0; i < uxArraySize; i++)
        {
            previousTaskStats[i] = currentTaskStats[i];
        }
    }

    pcWriteBuffer += strlen(pcWriteBuffer);

    // Print memory summary as well
    pcWriteBuffer += strlen(pcWriteBuffer);
    size_t internal_total_mem = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t spi_total_mem = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t internal_free_mem = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t spi_free_mem = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t spi_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t internal_low_wm = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t spi_low_wm = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    sprintf(pcWriteBuffer, "rp-m,%lu,Internal,%d,%d,%d,%d\n", curtime, internal_total_mem, internal_free_mem, internal_largest_free_block, internal_low_wm);
    pcWriteBuffer += strlen(pcWriteBuffer);
    sprintf(pcWriteBuffer, "rp-m,%lu,SPI,%d,%d,%d,%d\n", curtime, spi_total_mem, spi_free_mem, spi_largest_free_block, spi_low_wm);
    pcWriteBuffer += strlen(pcWriteBuffer);
}

void profile_task()
{
    static char output[1024];
    while (true)
    {
        GetRunTimeStats(output);
        printf(output);
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds{CONFIG_DTF_RUNTIME_PROFILING_SAMPLE_INTERVAL});
    }
}

void start_profiling(void)
{
    auto cfg = esp_pthread_get_default_config();
    cfg.thread_name = "profiling";
    cfg.pin_to_core = 0;
    cfg.stack_size = 1024 * 3;
    cfg.prio = 10;
    esp_pthread_set_cfg(&cfg);

    ESP_LOGI(LOG_TAG, "Runtime profiling starting...");
    std::thread profiling_thread(profile_task);
    profiling_thread.detach();
}

void stop_profiling(void)
{
    ESP_LOGI(LOG_TAG, "Runtime profiling stopping...");
    return;
}

#endif // defined(PROFILING)