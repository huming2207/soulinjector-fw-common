#pragma once

#include <esp_err.h>
#include <esp_log.h>
#include <lvgl.h>

namespace ui_state
{
    struct init_screen
    {
        char subtitle[32];
    };

    struct erase_screen
    {
        uint8_t percentage;
        char subtitle[32];
    };

    struct flash_screen
    {
        uint8_t percentage;
        char subtitle[32];
    };

    struct test_screen
    {
        uint8_t done_test;
        uint8_t total_test;
        char subtitle[32];
    };

    struct error_screen
    {
        esp_err_t ret;
        char comment[32];
        char qrcode[64];
    };

    enum display_state : int8_t
    {
        STATE_EMPTY = -1,
        STATE_INIT = 0,
        STATE_ERASE = 1,
        STATE_FLASH = 2,
        STATE_TEST  = 3,
        STATE_ERROR = 4,
        STATE_DONE = 5,
        STATE_USB = 6,
    };

    struct __attribute__((packed)) queue_item
    {
        display_state state;
        uint8_t percentage;
        uint8_t total_count;
        lv_color_t bg_color;
        char title[16];
        char comment[32];
        char qrcode[64];
    };
};

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
        BIT_READY = BIT(16),
        BIT_NOT_RENDERING = BIT(17),
    };

public:
    virtual void wait_and_render() = 0;
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