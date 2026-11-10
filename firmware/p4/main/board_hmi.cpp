#include "board_hmi.hpp"

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/clk_tree_defs.h"

namespace {

constexpr int kDisplayWidth = 480;
constexpr int kDisplayHeight = 800;
constexpr int kLcdResetGpio = 5;
constexpr int kBacklightGpio = 23;
constexpr int kTouchSdaGpio = 7;
constexpr int kTouchSclGpio = 8;
constexpr int kDsiLdoChannel = 3;
constexpr int kDsiLdoMillivolts = 2500;
constexpr int kDrawBufferLines = 64;

constexpr std::uint8_t kPage13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
constexpr std::uint8_t kPage10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
constexpr std::uint8_t kPage11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
constexpr std::uint8_t kPage00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
constexpr std::uint8_t kGammaPositive[] = {
    0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09,
    0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71,
};
constexpr std::uint8_t kGammaNegative[] = {
    0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08,
    0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D,
};
constexpr std::uint8_t kE1[] = {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40};
constexpr std::uint8_t kE2[] = {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00};
constexpr std::uint8_t kE5[] = {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0};
constexpr std::uint8_t kE8[] = {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0};
constexpr std::uint8_t kEd[] = {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B};
constexpr std::uint8_t kNoOperation[] = {0x00};
constexpr std::uint8_t kEf08[] = {0x08};
constexpr std::uint8_t kC0[] = {0x63, 0x00};
constexpr std::uint8_t kC1[] = {0x0D, 0x02};
constexpr std::uint8_t kC2[] = {0x10, 0x08};
constexpr std::uint8_t kCc[] = {0x10};
constexpr std::uint8_t kB0[] = {0x5D};
constexpr std::uint8_t kB1[] = {0x58};
constexpr std::uint8_t kB2[] = {0x87};
constexpr std::uint8_t kB3[] = {0x80};
constexpr std::uint8_t kB5[] = {0x4E};
constexpr std::uint8_t kB7[] = {0x85};
constexpr std::uint8_t kB8[] = {0x21};
constexpr std::uint8_t kB9[] = {0x10, 0x1F};
constexpr std::uint8_t kBb[] = {0x03};
constexpr std::uint8_t kBc[] = {0x00};
constexpr std::uint8_t kC1Page11[] = {0x78};
constexpr std::uint8_t kC2Page11[] = {0x78};
constexpr std::uint8_t kD0[] = {0x88};
constexpr std::uint8_t kE0[] = {0x00, 0x3A, 0x02};
constexpr std::uint8_t kE3[] = {0x00, 0x00, 0x33, 0x33};
constexpr std::uint8_t kE4[] = {0x44, 0x44};
constexpr std::uint8_t kE6[] = {0x00, 0x00, 0x33, 0x33};
constexpr std::uint8_t kE7[] = {0x44, 0x44};
constexpr std::uint8_t kEb[] = {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00};
constexpr std::uint8_t kEc[] = {0x08, 0x01};
constexpr std::uint8_t kEf[] = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54};

constexpr st7701_lcd_init_cmd_t kPanelInitCommands[] = {
    {0xFF, kPage13, sizeof(kPage13), 0},
    {0xEF, kEf08, sizeof(kEf08), 0},
    {0xFF, kPage10, sizeof(kPage10), 0},
    {0xC0, kC0, sizeof(kC0), 0},
    {0xC1, kC1, sizeof(kC1), 0},
    {0xC2, kC2, sizeof(kC2), 0},
    {0xCC, kCc, sizeof(kCc), 0},
    {0xB0, kGammaPositive, sizeof(kGammaPositive), 0},
    {0xB1, kGammaNegative, sizeof(kGammaNegative), 0},
    {0xFF, kPage11, sizeof(kPage11), 0},
    {0xB0, kB0, sizeof(kB0), 0},
    {0xB1, kB1, sizeof(kB1), 0},
    {0xB2, kB2, sizeof(kB2), 0},
    {0xB3, kB3, sizeof(kB3), 0},
    {0xB5, kB5, sizeof(kB5), 0},
    {0xB7, kB7, sizeof(kB7), 0},
    {0xB8, kB8, sizeof(kB8), 0},
    {0xB9, kB9, sizeof(kB9), 0},
    {0xBB, kBb, sizeof(kBb), 0},
    {0xBC, kBc, sizeof(kBc), 0},
    {0xC1, kC1Page11, sizeof(kC1Page11), 0},
    {0xC2, kC2Page11, sizeof(kC2Page11), 0},
    {0xD0, kD0, sizeof(kD0), 0},
    {0xE0, kE0, sizeof(kE0), 0},
    {0xE1, kE1, sizeof(kE1), 0},
    {0xE2, kE2, sizeof(kE2), 0},
    {0xE3, kE3, sizeof(kE3), 0},
    {0xE4, kE4, sizeof(kE4), 0},
    {0xE5, kE5, sizeof(kE5), 0},
    {0xE6, kE6, sizeof(kE6), 0},
    {0xE7, kE7, sizeof(kE7), 0},
    {0xE8, kE8, sizeof(kE8), 0},
    {0xEB, kEb, sizeof(kEb), 0},
    {0xEC, kEc, sizeof(kEc), 0},
    {0xED, kEd, sizeof(kEd), 0},
    {0xEF, kEf, sizeof(kEf), 0},
    {0xFF, kPage00, sizeof(kPage00), 0},
    {0x11, kNoOperation, 0, 120},
    {0x29, kNoOperation, 0, 20},
};

esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
esp_lcd_panel_io_handle_t dsi_io = nullptr;
esp_lcd_panel_handle_t lcd_panel = nullptr;
esp_lcd_panel_io_handle_t touch_io = nullptr;
esp_lcd_touch_handle_t touch = nullptr;
lv_display_t* lvgl_display = nullptr;
lv_indev_t* lvgl_touch = nullptr;
esp_ldo_channel_handle_t dsi_ldo = nullptr;
i2c_master_bus_handle_t touch_bus = nullptr;

esp_err_t init_dsi() noexcept {
    esp_ldo_channel_config_t ldo_config{};
    ldo_config.chan_id = kDsiLdoChannel;
    ldo_config.voltage_mv = kDsiLdoMillivolts;
    esp_err_t status = esp_ldo_acquire_channel(&ldo_config, &dsi_ldo);
    if (status != ESP_OK) {
        return status;
    }

    gpio_config_t output_config{};
    output_config.mode = GPIO_MODE_OUTPUT;
    output_config.pin_bit_mask = (1ULL << kLcdResetGpio) | (1ULL << kBacklightGpio);
    status = gpio_config(&output_config);
    if (status != ESP_OK) {
        return status;
    }
    status = gpio_set_level(static_cast<gpio_num_t>(kBacklightGpio), 0);
    if (status != ESP_OK) {
        return status;
    }
    status = gpio_set_level(static_cast<gpio_num_t>(kLcdResetGpio), 0);
    if (status != ESP_OK) {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    status = gpio_set_level(static_cast<gpio_num_t>(kLcdResetGpio), 1);
    if (status != ESP_OK) {
        return status;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_dsi_bus_config_t bus_config{};
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = 2;
    bus_config.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = 500;
    status = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
    if (status != ESP_OK) {
        return status;
    }

    const esp_lcd_dbi_io_config_t io_config = ST7701_PANEL_IO_DBI_CONFIG();
    status = esp_lcd_new_panel_io_dbi(dsi_bus, &io_config, &dsi_io);
    if (status != ESP_OK) {
        return status;
    }

    esp_lcd_dpi_panel_config_t dpi_config{};
    dpi_config.virtual_channel = 0;
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = 34;
    dpi_config.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi_config.out_color_format = LCD_COLOR_FMT_RGB565;
    dpi_config.num_fbs = 1;
    dpi_config.video_timing.h_size = kDisplayWidth;
    dpi_config.video_timing.v_size = kDisplayHeight;
    dpi_config.video_timing.hsync_pulse_width = 12;
    dpi_config.video_timing.hsync_back_porch = 42;
    dpi_config.video_timing.hsync_front_porch = 42;
    dpi_config.video_timing.vsync_pulse_width = 2;
    dpi_config.video_timing.vsync_back_porch = 8;
    dpi_config.video_timing.vsync_front_porch = 166;

    st7701_vendor_config_t vendor_config{};
    vendor_config.init_cmds = kPanelInitCommands;
    vendor_config.init_cmds_size = sizeof(kPanelInitCommands) / sizeof(kPanelInitCommands[0]);
    vendor_config.mipi_config.dsi_bus = dsi_bus;
    vendor_config.mipi_config.dpi_config = &dpi_config;
    vendor_config.flags.use_mipi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    status = esp_lcd_new_panel_st7701(dsi_io, &panel_config, &lcd_panel);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_lcd_panel_init(lcd_panel);
    if (status != ESP_OK) {
        return status;
    }
    status = esp_lcd_panel_disp_on_off(lcd_panel, true);
    if (status != ESP_OK) {
        return status;
    }

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    status = lvgl_port_init(&lvgl_config);
    if (status != ESP_OK) {
        return status;
    }
    lvgl_port_display_cfg_t display_config{};
    display_config.io_handle = dsi_io;
    display_config.panel_handle = lcd_panel;
    display_config.buffer_size = kDisplayWidth * kDrawBufferLines;
    display_config.double_buffer = true;
    display_config.hres = kDisplayWidth;
    display_config.vres = kDisplayHeight;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    display_config.flags.buff_dma = true;
    const lvgl_port_display_dsi_cfg_t dsi_display_config{};
    lvgl_display = lvgl_port_add_disp_dsi(&display_config, &dsi_display_config);
    if (lvgl_display == nullptr) {
        return ESP_FAIL;
    }

    return gpio_set_level(static_cast<gpio_num_t>(kBacklightGpio), 1);
}

esp_err_t init_touch() noexcept {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(kTouchSdaGpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(kTouchSclGpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    esp_err_t status = i2c_new_master_bus(&bus_config, &touch_bus);
    if (status != ESP_OK) {
        return status;
    }

    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.lcd_param_bits = 0;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = 100000;
    status = esp_lcd_new_panel_io_i2c(touch_bus, &io_config, &touch_io);
    if (status != ESP_OK) {
        return status;
    }

    esp_lcd_touch_io_gt911_config_t gt911_config = {
        .dev_addr = static_cast<uint8_t>(io_config.dev_addr),
    };
    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = kDisplayWidth;
    touch_config.y_max = kDisplayHeight;
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_NC;
    touch_config.driver_data = &gt911_config;
    status = esp_lcd_touch_new_i2c_gt911(touch_io, &touch_config, &touch);
    if (status != ESP_OK) {
        return status;
    }

    lvgl_port_touch_cfg_t lvgl_touch_config{};
    lvgl_touch_config.disp = lvgl_display;
    lvgl_touch_config.handle = touch;
    lvgl_touch = lvgl_port_add_touch(&lvgl_touch_config);
    return lvgl_touch == nullptr ? ESP_FAIL : ESP_OK;
}

}

namespace hub::board {

esp_err_t init_hmi(lv_display_t** display) noexcept {
    if (display == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *display = nullptr;

    esp_err_t status = init_dsi();
    if (status != ESP_OK) {
        return status;
    }
    status = init_touch();
    if (status != ESP_OK) {
        return status;
    }

    *display = lvgl_display;
    return ESP_OK;
}

}