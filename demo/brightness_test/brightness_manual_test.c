#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "brightness_test";

// Test GPIO5 as potential display enable pin
void test_gpio5_enable(void)
{
    ESP_LOGI(TAG, "=== Testing GPIO5 as Display Enable ===");
    
    // Configure GPIO5 as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 5),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "GPIO5 configured as output");
    
    // Try both states
    ESP_LOGI(TAG, "Setting GPIO5 LOW");
    gpio_set_level(5, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "Setting GPIO5 HIGH");
    gpio_set_level(5, 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "GPIO5 test complete - keeping HIGH");
}

// Create visible content for brightness testing
void create_test_screen(void)
{
    ESP_LOGI(TAG, "Creating test screen with visible content");
    
    // Create a simple white screen with text
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    
    // Large text label
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "BRIGHTNESS TEST\n\n"
                             "Watch this text\n"
                             "get brighter and\n"
                             "dimmer\n\n"
                             "0% = Darkest\n"
                             "100% = Brightest");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    
    // Add colored rectangles for better visibility
    lv_obj_t *rect1 = lv_obj_create(scr);
    lv_obj_set_size(rect1, 100, 50);
    lv_obj_set_pos(rect1, 50, 50);
    lv_obj_set_style_bg_color(rect1, lv_color_make(255, 0, 0), 0); // Red
    
    lv_obj_t *rect2 = lv_obj_create(scr);
    lv_obj_set_size(rect2, 100, 50);
    lv_obj_set_pos(rect2, 330, 50);
    lv_obj_set_style_bg_color(rect2, lv_color_make(0, 255, 0), 0); // Green
    
    lv_obj_t *rect3 = lv_obj_create(scr);
    lv_obj_set_size(rect3, 100, 50);
    lv_obj_set_pos(rect3, 190, 650);
    lv_obj_set_style_bg_color(rect3, lv_color_make(0, 0, 255), 0); // Blue
}

// Manual PWM test with visible content
void test_brightness_with_content(void)
{
    ESP_LOGI(TAG, "=== Brightness Test with Visible Content ===");
    
    // Get the GPIO pin from config
    gpio_num_t backlight_pin = BSP_LCD_BACKLIGHT;
    ESP_LOGI(TAG, "Backlight GPIO: %d", backlight_pin);
    
    // Configure LEDC timer
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = CONFIG_BSP_JC4880P443C_BACKLIGHT_TIMER,
        .freq_hz = CONFIG_BSP_JC4880P443C_BACKLIGHT_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&timer_config);
    ESP_LOGI(TAG, "LEDC timer config: %s", esp_err_to_name(ret));
    
    // Configure LEDC channel
    ledc_channel_config_t channel_config = {
        .gpio_num = backlight_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CONFIG_BSP_JC4880P443C_BACKLIGHT_CHANNEL,
        .duty = 0,
        .timer_sel = CONFIG_BSP_JC4880P443C_BACKLIGHT_TIMER
    };
    ret = ledc_channel_config(&channel_config);
    ESP_LOGI(TAG, "LEDC channel config: %s", esp_err_to_name(ret));
    
    // Test different duty cycles with longer delays for observation
    uint32_t duty_values[] = {0, 102, 256, 512, 768, 921, 1023}; // 0%, 10%, 25%, 50%, 75%, 90%, 100%
    const char* percentages[] = {"0%", "10%", "25%", "50%", "75%", "90%", "100%"};
    
    for (int cycle = 0; cycle < 3; cycle++) { // Repeat 3 times
        ESP_LOGI(TAG, "=== Brightness Cycle %d ===", cycle + 1);
        
        for (int i = 0; i < 7; i++) {
            ESP_LOGI(TAG, "Setting brightness to %s (duty: %lu)", percentages[i], duty_values[i]);
            
            ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_JC4880P443C_BACKLIGHT_CHANNEL, duty_values[i]);
            ESP_LOGI(TAG, "ledc_set_duty: %s", esp_err_to_name(ret));
            
            ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BSP_JC4880P443C_BACKLIGHT_CHANNEL);
            ESP_LOGI(TAG, "ledc_update_duty: %s", esp_err_to_name(ret));
            
            ESP_LOGI(TAG, "*** LOOK AT SCREEN NOW - Should see %s brightness ***", percentages[i]);
            vTaskDelay(pdMS_TO_TICKS(5000)); // Hold for 5 seconds for clear observation
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== JC4880P443C Display and Brightness Test ===");
    
    // Test GPIO5 first (potential display enable)
    test_gpio5_enable();
    
    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 80,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        abort();
    }
    ESP_LOGI(TAG, "Display started successfully");
    
    // Turn on backlight initially at full brightness
    esp_err_t ret = bsp_display_backlight_on();
    ESP_LOGI(TAG, "Backlight initialization: %s", esp_err_to_name(ret));
    
    // Create visible content
    bsp_display_lock(0);
    create_test_screen();
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Screen content created. You should see white screen with text and colored rectangles.");
    vTaskDelay(pdMS_TO_TICKS(3000)); // Give time to see the screen
    
    // Test brightness with visible content
    test_brightness_with_content();
    
    ESP_LOGI(TAG, "Brightness test complete!");
    ESP_LOGI(TAG, "Results:");
    ESP_LOGI(TAG, "- If you saw the screen content get brighter/dimmer, brightness control works!");
    ESP_LOGI(TAG, "- If screen stayed black, check GPIO5 enable pin or backlight circuit");
    ESP_LOGI(TAG, "- If brightness didn't change, check PWM GPIO configuration");
}