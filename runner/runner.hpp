#pragma once

#include <esp_err.h>

class runner_if
{
public:
    virtual esp_err_t start() = 0;
    virtual esp_err_t stop() = 0;
    virtual esp_err_t handle_task() = 0;
};