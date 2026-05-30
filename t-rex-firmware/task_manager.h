#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define TASK_RESULT_QUEUE_SIZE 64
#define TASK_STACK_DEFAULT     8192
#define TASK_STACK_SMALL       4096

struct TaskResult {
    enum Type : uint8_t { PORT_OPEN, HOST_FOUND, INFO, DONE } type;
    char data[48];
};

class TaskManager {
public:
    static bool start(TaskFunction_t fn, const char* name, void* params,
                      uint32_t stackSize = TASK_STACK_DEFAULT, UBaseType_t core = 0);
    static void requestStop();
    static bool isRunning();
    static void cleanup();

    static volatile bool stopRequested;
    static volatile bool taskRunning;
    static QueueHandle_t resultQueue;
    static TaskHandle_t  handle;
};

#endif // TASK_MANAGER_H
