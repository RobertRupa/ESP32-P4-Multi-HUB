#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace hub::board {

esp_err_t init_hmi(lv_display_t** display) noexcept;

}