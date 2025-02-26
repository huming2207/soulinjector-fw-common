#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <lvgl.h>

class ui_composer
{
public:
    enum state : uint32_t
    {
        INIT = 0,
        ERASE = 1,
        FLASH = 2,
        TEST  = 3,
        ERROR = 4,
        DONE = 5,
        USB = 6,
        CONFIG = 7,
        BIT_READY = (1u << 16u),
        BIT_NOT_RENDERING = (1u << 17u),
    };

public:
    virtual esp_err_t init() = 0;
    virtual esp_err_t display_init() = 0;
    virtual esp_err_t display_erase(uint8_t percentage) = 0;
    virtual esp_err_t display_test(size_t done, size_t total, const char *test_msg) = 0;
    virtual esp_err_t display_program(uint8_t percentage) = 0;
    virtual esp_err_t display_done() = 0;
    virtual esp_err_t display_error(const char *header, const char *err_msg) = 0;
    virtual esp_err_t display_config() = 0;
    virtual esp_err_t display_current(double min_ua, double max_ua, double avg_ua, const char *state, lv_color_t state_color) = 0;
    virtual void wait_and_start_render() = 0;
    virtual void render_done() = 0;
};

class ui_screen
{
public:
    enum state
    {
        CLEAR = 0,
        PROGRESS = 1,
        CURRENT = 2,
        MESSAGE = 3,
    };

public:
    virtual esp_err_t init(lv_obj_t *_base_obj) = 0;
    virtual void deinit() = 0;

protected:
    lv_obj_t *base_obj = nullptr;
};