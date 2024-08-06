/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_st7701.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/ledc.h"

#include "type_9_init_cmds.h"

static const char *TAG = "lcd-display";

#define I2C_MASTER_SDA_IO 17
#define I2C_MASTER_SCL_IO 18
#define I2C_MASTER_FREQ_HZ 400 * 1000 // 400kHz is the max speed of the XL9535
#define I2C_TX_BUF_DISABLE 0          // I2C master do not need buffer
#define I2C_RX_BUF_DISABLE 0          // I2C master do not need buf`fer

#define LCD_PCLK_IO 41
#define LCD_VSYNC_IO 40
#define LCD_HSYNC_IO 39
#define LCD_B0_IO 1
#define LCD_B1_IO 2
#define LCD_B2_IO 3
#define LCD_B3_IO 4
#define LCD_B4_IO 5
#define LCD_G0_IO 6
#define LCD_G1_IO 7
#define LCD_G2_IO 8
#define LCD_G3_IO 9
#define LCD_G4_IO 10
#define LCD_G5_IO 11
#define LCD_R0_IO 12
#define LCD_R1_IO 13
#define LCD_R2_IO 42
#define LCD_R3_IO 46
#define LCD_R4_IO 45
#define LCD_BL_IO 14

#define LCD_IO_UNUSED -1

#define BACKLIGHT_TIMER_RESOLUTION LEDC_TIMER_8_BIT

void app_main(void)
{
    ESP_LOGI(TAG, "Configuring backlight");
    gpio_set_direction(LCD_BL_IO, TCP_OUTPUT_DEBUG);
    gpio_set_level(LCD_BL_IO, 0);

    ledc_timer_config_t backlight_timer_config = {
        .timer_num = LEDC_TIMER_0,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .clk_cfg = LEDC_USE_XTAL_CLK,
        .duty_resolution = BACKLIGHT_TIMER_RESOLUTION,
        .freq_hz = 40000,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&backlight_timer_config));
    ledc_channel_config_t backlight_channel_config = {
        .gpio_num = LCD_BL_IO,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty = (1 << (BACKLIGHT_TIMER_RESOLUTION - 1)) + (BACKLIGHT_TIMER_RESOLUTION - 1),
    };
    ESP_ERROR_CHECK(ledc_channel_config(&backlight_channel_config));

    ESP_LOGI(TAG, "Configuring I2C port %u", I2C_NUM_1);
    i2c_port_t i2c_port = I2C_NUM_1;
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO, // select GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO, // select GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ, // select frequency specific to your project
        // .clk_flags = 0,          /*!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. */
    };
    ESP_ERROR_CHECK(i2c_param_config(i2c_port, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(i2c_port, i2c_conf.mode, I2C_RX_BUF_DISABLE, I2C_TX_BUF_DISABLE, 0));

    esp_io_expander_handle_t io_expander = NULL;
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca95xx_16bit(i2c_port, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &io_expander));
    ESP_LOGI(TAG, "XL9535 found");
    ESP_LOGI(TAG, "Resetting");
    // esp_io_expander_print_state(io_expander);
    ESP_ERROR_CHECK(io_expander->reset(io_expander));
    // esp_io_expander_print_state(io_expander);
    // ESP_LOGI(TAG, "Configuring as all output");
    // ESP_ERROR_CHECK(io_expander->write_direction_reg(io_expander, 0x0000));
    // esp_io_expander_print_state(io_expander);
    // ESP_LOGI(TAG, "Configuring a couple as inputs");
    // ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_14, IO_EXPANDER_INPUT));
    // ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT));
    // ESP_ERROR_CHECK(esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_2, IO_EXPANDER_INPUT));
    // esp_io_expander_print_state(io_expander);

    // Set up 3-wire SPI for display command channel

    ESP_LOGI(TAG, "Configuring three wire command SPI");
    spi_line_config_t spi_line = {
        .cs_io_type = IO_TYPE_EXPANDER,
        .cs_expander_pin = IO_EXPANDER_PIN_NUM_17,
        .scl_io_type = IO_TYPE_EXPANDER,
        .scl_expander_pin = IO_EXPANDER_PIN_NUM_15,
        .sda_io_type = IO_TYPE_EXPANDER,
        .sda_expander_pin = IO_EXPANDER_PIN_NUM_16,
        .io_expander = io_expander, // Created by the user
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(spi_line, false);
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &panel_io));

    ESP_ERROR_CHECK(esp_io_expander_print_state(io_expander));

    ESP_LOGI(TAG, "Install ST7701 panel driver");
    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = ST7701_480_480_PANEL_60HZ_RGB_TIMING(),
        .data_width = 16,
        .bits_per_pixel = 16, // RGB565
        .num_fbs = 2,
        .bounce_buffer_size_px = 0,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_HSYNC_IO,
        .vsync_gpio_num = LCD_VSYNC_IO,
        .de_gpio_num = LCD_IO_UNUSED,
        .pclk_gpio_num = LCD_PCLK_IO,
        .disp_gpio_num = LCD_IO_UNUSED,
        .data_gpio_nums = {
            LCD_B0_IO,
            LCD_B1_IO,
            LCD_B2_IO,
            LCD_B3_IO,
            LCD_B4_IO,
            LCD_G0_IO,
            LCD_G1_IO,
            LCD_G2_IO,
            LCD_G3_IO,
            LCD_G4_IO,
            LCD_G5_IO,
            LCD_R0_IO,
            LCD_R1_IO,
            LCD_R2_IO,
            LCD_R3_IO,
            LCD_R4_IO,
        },
        .flags = {.double_fb = true, .fb_in_psram = true, .no_fb = false, .bb_invalidate_cache = false, .disp_active_low = false, .refresh_on_demand = true}};
    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = st7701_type9_init_operations, // Uncomment these line if use custom initialization commands
        .init_cmds_size = sizeof(st7701_type9_init_operations) / sizeof(st7701_lcd_init_cmd_t),
        .flags = {
            // Set to 1 if panel IO is no longer needed after LCD initialization.
            // If the panel IO pins are sharing other pins of the RGB interface to save GPIOs,
            // Please set it to 1 to release the pins.
            .enable_io_multiplex = false,
            .mirror_by_cmd = false,
            .use_mipi_interface = false,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_IO_UNUSED,              // Set to -1 if not use
        .rgb_ele_order = COLOR_RGB_ELEMENT_ORDER_BGR, // Implemented by LCD command `36h`
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(panel_io, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "Successfully built ST7701 panel interface");

    // Log state
    esp_io_expander_print_state(io_expander);
}
