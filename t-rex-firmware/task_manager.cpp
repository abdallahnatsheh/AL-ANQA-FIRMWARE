#include "task_manager.h"

volatile bool TaskManager::stopRequested = false;
volatile bool TaskManager::taskRunning   = false;
QueueHandle_t TaskManager::resultQueue   = nullptr;
TaskHandle_t  TaskManager::handle        = nullptr;

bool TaskManager::start(TaskFunction_t fn, const char* name, void* params,
                         uint32_t stackSize, UBaseType_t core) {
    if (taskRunning) return false;

    stopRequested = false;
    taskRunning   = true;
    resultQueue   = xQueueCreate(TASK_RESULT_QUEUE_SIZE, sizeof(TaskResult));

    BaseType_t ret = xTaskCreatePinnedToCore(fn, name, stackSize, params, 2, &handle, core);
    if (ret != pdPASS) {
        taskRunning = false;
        if (resultQueue) { vQueueDelete(resultQueue); resultQueue = nullptr; }
        return false;
    }
    return true;
}

void TaskManager::requestStop() { stopRequested = true; }
bool TaskManager::isRunning()   { return taskRunning; }

void TaskManager::cleanup() {
    handle = nullptr;
    if (resultQueue) { vQueueDelete(resultQueue); resultQueue = nullptr; }
    taskRunning   = false;
    stopRequested = false;
}
