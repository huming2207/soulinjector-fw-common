#pragma once

#include <esp_err.h>

class reporter_if
{
public:
    virtual esp_err_t send_target_ident(const uint8_t *target_sn, size_t sn_len, const uint8_t *fw_sha256, const uint8_t *algo_sha256, uint32_t wait_ticks) = 0;
    virtual esp_err_t send_test_report(const uint8_t *payload, size_t len, uint32_t wait_ticks) = 0;
};

class report_response_if
{
public:
    virtual esp_err_t on_report_response(esp_err_t state, const uint8_t *payload, size_t len) = 0;
};