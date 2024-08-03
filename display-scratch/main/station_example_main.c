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

#include "type_9_init_cmds.h"


/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &event_handler,
                    NULL,
                    &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &event_handler,
                    NULL,
                    &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

#define I2C_MASTER_SDA_IO 17
#define I2C_MASTER_SCL_IO 18
#define I2C_MASTER_FREQ_HZ 400 * 1000 // 400kHz is the max speed of the XL9535
#define I2C_TX_BUF_DISABLE 0          // I2C master do not need buffer
#define I2C_RX_BUF_DISABLE 0          // I2C master do not need buffer

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

void app_main(void)
{
    // Initialize NVS
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    // {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);

    // ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    // wifi_init_sta();

    // Setup XL9535 IO expander

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
        .flags = {
            .double_fb = true,
            .fb_in_psram = true,
            .no_fb = false,
            .bb_invalidate_cache = false,
            .disp_active_low = false,
            .refresh_on_demand = true
        }
    };
    st7701_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = st7701_type9_init_operations,      // Uncomment these line if use custom initialization commands
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
        .reset_gpio_num = LCD_IO_UNUSED,                   // Set to -1 if not use
        .rgb_ele_order = COLOR_RGB_ELEMENT_ORDER_BGR,     // Implemented by LCD command `36h`
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(panel_io, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "Successfully built ST7701 panel interface");

    // Log state
    esp_io_expander_print_state(io_expander);
}
