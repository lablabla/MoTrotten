/**
 * MoTrotten Hardware Test Firmware
 *
 * Standalone app_main for verifying individual hardware components.
 *
 * To use: in main/CMakeLists.txt, replace "app_main.cpp" with "hw_test.cpp"
 * in the SRCS list, then rebuild and flash.
 *
 * Interact via serial monitor (115200 baud) using the menu keys.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "VL53L0X.h"
#include "desk_config.h"

// ─── ADC singleton (shared across tests) ────────────────────────────────────

static adc_oneshot_unit_handle_t s_adc = nullptr;

static bool adc_ready() {
    if (s_adc) return true;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = MOTOR_IS_ADC_UNIT,
        .clk_src  = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        printf("  ERROR: ADC unit init failed\n");
        return false;
    }
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc, MOTOR_IS_CH_R,    &ch_cfg);
    adc_oneshot_config_channel(s_adc, MOTOR_IS_CH_L,    &ch_cfg);
    adc_oneshot_config_channel(s_adc, BTN_ADC_CHANNEL,  &ch_cfg);
    return true;
}

// ─── LEDC singleton ──────────────────────────────────────────────────────────

static bool s_ledc_init = false;

static bool ledc_ready() {
    if (s_ledc_init) return true;
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = MOTOR_LEDC_MODE,
        .duty_resolution = MOTOR_LEDC_RES,
        .timer_num       = MOTOR_LEDC_TIMER,
        .freq_hz         = MOTOR_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        printf("  ERROR: LEDC timer init failed\n");
        return false;
    }
    ledc_channel_config_t ch_r = {
        .gpio_num   = PIN_MOTOR_PWM_R,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_LEDC_CH_R,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config_t ch_l = {
        .gpio_num   = PIN_MOTOR_PWM_L,
        .speed_mode = MOTOR_LEDC_MODE,
        .channel    = MOTOR_LEDC_CH_L,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = MOTOR_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ch_r) != ESP_OK ||
        ledc_channel_config(&ch_l) != ESP_OK) {
        printf("  ERROR: LEDC channel init failed\n");
        return false;
    }
    gpio_config_t en_cfg = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_EN_R) | (1ULL << PIN_MOTOR_EN_L),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&en_cfg);
    gpio_set_level(PIN_MOTOR_EN_R, 0);
    gpio_set_level(PIN_MOTOR_EN_L, 0);
    s_ledc_init = true;
    return true;
}

// ─── helpers ─────────────────────────────────────────────────────────────────

static void wait_enter() {
    printf("  Press ENTER to continue...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != '\r') {}
}

static void motor_off() {
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_R);
    ledc_set_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, MOTOR_LEDC_CH_L);
    gpio_set_level(PIN_MOTOR_EN_R, 0);
    gpio_set_level(PIN_MOTOR_EN_L, 0);
}

// ─── TEST 1: EN GPIO toggle ───────────────────────────────────────────────────

static void test_gpio() {
    printf("\n=== TEST 1: EN GPIO Toggle ===\n");
    printf("  Measure GPIO%d and GPIO%d with multimeter.\n",
           PIN_MOTOR_EN_R, PIN_MOTOR_EN_L);
    printf("  Expect 3 alternating HIGH/LOW pulses (500ms each).\n");
    if (!ledc_ready()) return; // also inits EN GPIOs

    for (int i = 0; i < 3; i++) {
        gpio_set_level(PIN_MOTOR_EN_R, 1);
        gpio_set_level(PIN_MOTOR_EN_L, 0);
        printf("  [%d] EN_R=HIGH  EN_L=LOW\n", i);
        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_set_level(PIN_MOTOR_EN_R, 0);
        gpio_set_level(PIN_MOTOR_EN_L, 1);
        printf("  [%d] EN_R=LOW   EN_L=HIGH\n", i);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    motor_off();
    printf("  PASS if GPIO levels matched above.\n");
}

// ─── TEST 2: PWM output ───────────────────────────────────────────────────────

static void test_pwm() {
    printf("\n=== TEST 2: PWM Output ===\n");
    printf("  Probe GPIO%d (PWM_R / UP) and GPIO%d (PWM_L / DOWN) with scope.\n",
           PIN_MOTOR_PWM_R, PIN_MOTOR_PWM_L);
    printf("  Expect %dHz, 10-bit PWM ramping 0%%→100%%→0%% on each channel.\n",
           MOTOR_LEDC_FREQ_HZ);
    if (!ledc_ready()) return;

    for (int ch = 0; ch < 2; ch++) {
        ledc_channel_t channel = (ch == 0) ? MOTOR_LEDC_CH_R : MOTOR_LEDC_CH_L;
        const char* label = (ch == 0) ? "PWM_R (UP)" : "PWM_L (DOWN)";
        printf("  Ramping %s...\n", label);
        for (int duty = 0; duty <= MOTOR_MAX_DUTY; duty += 51) {
            ledc_set_duty(MOTOR_LEDC_MODE, channel, duty);
            ledc_update_duty(MOTOR_LEDC_MODE, channel);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        for (int duty = MOTOR_MAX_DUTY; duty >= 0; duty -= 51) {
            ledc_set_duty(MOTOR_LEDC_MODE, channel, duty);
            ledc_update_duty(MOTOR_LEDC_MODE, channel);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    motor_off();
    printf("  PASS if both channels showed clean PWM on scope.\n");
}

// ─── TEST 3: Motor run UP ────────────────────────────────────────────────────

static void test_motor(bool up) {
    printf("\n=== TEST %c: Motor Run %s ===\n", up ? '3' : '4', up ? "UP" : "DOWN");
    printf("  CAUTION: desk will move ~2 seconds.\n");
    printf("  Ensure clearance. Desk must be away from travel limits.\n");
    wait_enter();
    if (!ledc_ready()) return;

    gpio_set_level(PIN_MOTOR_EN_R, 1);
    gpio_set_level(PIN_MOTOR_EN_L, 1);

    ledc_channel_t active_ch = up ? MOTOR_LEDC_CH_R : MOTOR_LEDC_CH_L;
    ledc_channel_t idle_ch   = up ? MOTOR_LEDC_CH_L : MOTOR_LEDC_CH_R;
    
    // Ensure the idle side is firmly anchored to 0V (GND)
    ledc_set_duty(MOTOR_LEDC_MODE, idle_ch, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, idle_ch);

    // Ramp up over MOTOR_RAMP_MS
    const int steps = 20;
    for (int i = 0; i <= steps; i++) {
        uint32_t duty = (uint32_t)i * MOTOR_MAX_DUTY / steps;
        ledc_set_duty(MOTOR_LEDC_MODE, active_ch, duty);
        ledc_update_duty(MOTOR_LEDC_MODE, active_ch);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_MS / steps));
    }
    printf("  Full speed. Running 2s...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Ramp down
    for (int i = steps; i >= 0; i--) {
        uint32_t duty = (uint32_t)i * MOTOR_MAX_DUTY / steps;
        ledc_set_duty(MOTOR_LEDC_MODE, active_ch, duty);
        ledc_update_duty(MOTOR_LEDC_MODE, active_ch);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_MS / steps));
    }
    motor_off();
    printf("  PASS if desk moved %s smoothly without noise or stall.\n",
           up ? "up" : "down");
}

// ─── TEST 5: Current sense ADC ────────────────────────────────────────────────

static void is_print_reading(int n) {
    int r = 0, l = 0;
    adc_oneshot_read(s_adc, MOTOR_IS_CH_R, &r);
    adc_oneshot_read(s_adc, MOTOR_IS_CH_L, &l);
    float vr = r * 3.3f / 4095.0f;
    float vl = l * 3.3f / 4095.0f;
    float ir = vr / 2200.0f * 8500.0f;
    float il = vl / 2200.0f * 8500.0f;
    printf("  [%2d] IS_R: raw=%4d %.3fV %.2fA | IS_L: raw=%4d %.3fV %.2fA\n",
           n, r, vr, ir, l, vl, il);
}

// Run motor in one direction while sampling IS every 200ms.
// Sequence: ramp up → full speed 2s → ramp down.
static void motor_run_with_is(bool up) {
    ledc_channel_t active_ch = up ? MOTOR_LEDC_CH_R : MOTOR_LEDC_CH_L;
    ledc_channel_t idle_ch   = up ? MOTOR_LEDC_CH_L : MOTOR_LEDC_CH_R;
    const int steps  = 20;
    int reading = 0;

    gpio_set_level(PIN_MOTOR_EN_R, 1);
    gpio_set_level(PIN_MOTOR_EN_L, 1);
    ledc_set_duty(MOTOR_LEDC_MODE, idle_ch, 0);
    ledc_update_duty(MOTOR_LEDC_MODE, idle_ch);

    printf("  >> Ramp UP (%s)\n", up ? "UP" : "DOWN");
    for (int i = 0; i <= steps; i++) {
        uint32_t duty = (uint32_t)i * MOTOR_MAX_DUTY / steps;
        ledc_set_duty(MOTOR_LEDC_MODE, active_ch, duty);
        ledc_update_duty(MOTOR_LEDC_MODE, active_ch);
        is_print_reading(reading++);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_MS / steps));
    }

    printf("  >> Full speed 2s\n");
    int64_t end = esp_timer_get_time() + 2000000LL;
    while (esp_timer_get_time() < end) {
        is_print_reading(reading++);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    printf("  >> Ramp DOWN\n");
    for (int i = steps; i >= 0; i--) {
        uint32_t duty = (uint32_t)i * MOTOR_MAX_DUTY / steps;
        ledc_set_duty(MOTOR_LEDC_MODE, active_ch, duty);
        ledc_update_duty(MOTOR_LEDC_MODE, active_ch);
        is_print_reading(reading++);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RAMP_MS / steps));
    }
    motor_off();
}

static void test_current_sense() {
    printf("\n=== TEST 5: Current Sense ADC ===\n");
    printf("  R_IS=2200Ω  kILIS=8500  Vref=3.3V  12-bit\n");
    printf("  I = (raw * 3.3 / 4095) / 2200 * 8500\n");
    printf("  Stall threshold: %d raw (~%.1fA)\n",
           MOTOR_STALL_THRESHOLD_RAW,
           (float)MOTOR_STALL_THRESHOLD_RAW * 3.3f / 4095.0f / 2200.0f * 8500.0f);
    printf("  CAUTION: desk will move UP then DOWN. Ensure clearance.\n");
    wait_enter();
    if (!adc_ready() || !ledc_ready()) return;

    printf("\n  --- At rest ---\n");
    for (int i = 0; i < 5; i++) { is_print_reading(i); vTaskDelay(pdMS_TO_TICKS(200)); }

    printf("\n  --- Motor UP ---\n");
    motor_run_with_is(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    printf("\n  --- At rest ---\n");
    for (int i = 0; i < 3; i++) { is_print_reading(i); vTaskDelay(pdMS_TO_TICKS(200)); }

    printf("\n  --- Motor DOWN ---\n");
    motor_run_with_is(false);
    vTaskDelay(pdMS_TO_TICKS(500));

    printf("\n  --- At rest ---\n");
    for (int i = 0; i < 5; i++) { is_print_reading(i); vTaskDelay(pdMS_TO_TICKS(200)); }

    printf("\n  PASS: rest < 50 raw, movement > 0, no reading >= %d (stall threshold).\n",
           MOTOR_STALL_THRESHOLD_RAW);
    printf("  Use the 'full speed' readings to tune MOTOR_STALL_THRESHOLD_RAW.\n");
}

// ─── TEST 6: Button ADC ───────────────────────────────────────────────────────

static const char* decode_btn(int raw) {
    if (raw < BTN_UP_MAX)                                   return "UP";
    if (raw >= BTN_DOWN_MIN    && raw <= BTN_DOWN_MAX)      return "DOWN";
    if (raw >= BTN_PRESET1_MIN && raw <= BTN_PRESET1_MAX)   return "PRESET1";
    if (raw >= BTN_PRESET2_MIN && raw <= BTN_PRESET2_MAX)   return "PRESET2";
    if (raw >= BTN_NONE_MIN)                                return "NONE";
    return "?";
}

static void test_buttons() {
    printf("\n=== TEST 6: Button ADC Ladder ===\n");
    printf("  GPIO%d (ADC1_CH%d), 10kΩ pull-up, 3.3V, 12-bit.\n",
           PIN_ADC_BTN, BTN_ADC_CHANNEL);
    printf("  Expected raw values: UP<100  DOWN 250-500  PRESET1 600-900  PRESET2 1150-1450\n");
    printf("  Press each button when prompted. 30 readings, 300ms apart:\n");
    if (!adc_ready()) return;

    for (int i = 0; i < 30; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc, BTN_ADC_CHANNEL, &raw);
        printf("  [%2d] raw=%4d => %-8s\n", i, raw, decode_btn(raw));
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    printf("  PASS if each physical button mapped to the correct label above.\n");
}

// ─── TEST 7: VL53L0X I2C ─────────────────────────────────────────────────────

static void test_vl53l0x() {
    printf("\n=== TEST 7: VL53L0X I2C ===\n");
    printf("  SDA=GPIO%d  SCL=GPIO%d  freq=%dkHz  addr=0x%02X\n",
           PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ / 1000, VL53L0X_ADDR);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = false },
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        printf("  ERROR: I2C bus init failed. Check SDA/SCL wiring.\n");
        return;
    }

    printf("  Scanning I2C bus (0x01–0x7E)...\n");
    int found = 0;
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        if (i2c_master_probe(bus, addr, 10) == ESP_OK) {
            printf("  Found: 0x%02X%s\n", addr,
                   addr == VL53L0X_ADDR ? "  <-- VL53L0X" : "");
            found++;
        }
    }
    if (!found) {
        printf("  No devices found. Check wiring, power (2.8V), and pull-ups.\n");
        i2c_del_master_bus(bus);
        return;
    }

    // Read model ID register: 0xC0 should return 0xEE on VL53L0X
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = VL53L0X_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) == ESP_OK) {
        uint8_t reg = 0xC0;
        uint8_t id  = 0;
        esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, &id, 1, 100);
        if (ret == ESP_OK) {
            printf("  Model ID (reg 0xC0) = 0x%02X  (expect 0xEE) %s\n",
                   id, id == 0xEE ? "OK" : "MISMATCH — wrong device?");
        } else {
            printf("  Model ID read failed: %s\n", esp_err_to_name(ret));
        }
        i2c_master_bus_rm_device(dev);
    }

    i2c_del_master_bus(bus);
    printf("  PASS if device found at 0x29 and model ID = 0xEE.\n");
}

// ─── TEST t: VL53L0X range readings ──────────────────────────────────────────

static void test_tof_range() {
    printf("\n=== TEST t: VL53L0X Range Readings ===\n");
    printf("  SDA=GPIO%d  SCL=GPIO%d  addr=0x%02X\n",
           PIN_I2C_SDA, PIN_I2C_SCL, VL53L0X_ADDR);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags             = { .enable_internal_pullup = false },
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        printf("  ERROR: I2C bus init failed.\n");
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = VL53L0X_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        printf("  ERROR: Failed to add I2C device.\n");
        i2c_del_master_bus(bus);
        return;
    }

    VL53L0X sensor(dev);
    sensor.setTimeout(500);

    if (!sensor.init()) {
        printf("  ERROR: VL53L0X init failed. Check power and pull-ups.\n");
        i2c_master_bus_rm_device(dev);
        i2c_del_master_bus(bus);
        return;
    }
    printf("  Sensor init OK. 30 readings, 200ms apart.\n");
    printf("  Move your hand over the sensor to verify range changes.\n\n");

    int ok = 0, errors = 0;
    for (int i = 0; i < 30; i++) {
        uint16_t mm = sensor.readRangeSingleMillimeters();
        if (sensor.timeoutOccurred() || mm == 65535) {
            printf("  [%2d] TIMEOUT / out of range\n", i);
            errors++;
        } else {
            printf("  [%2d] %4d mm  (%.1f cm)\n", i, mm, mm / 10.0f);
            ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    printf("\n  %d/%d readings valid.\n", ok, ok + errors);
    printf("  PASS if readings change as you move an object over the sensor.\n");
    printf("  Typical desk range: 600–1200mm. Values >8190mm = no target.\n");

    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
}

// ─── TEST 8: Limit switch ────────────────────────────────────────────────────

static void test_limit_switch() {
    printf("\n=== TEST 8: Limit Switch ===\n");
    printf("  GPIO%d, active low, external 10kΩ pull-up.\n", PIN_LIMIT_SW);
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_LIMIT_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    printf("  20 readings, 500ms apart. Activate the limit switch mid-test:\n");
    for (int i = 0; i < 20; i++) {
        int lvl = gpio_get_level(PIN_LIMIT_SW);
        printf("  [%2d] GPIO%d = %d  %s\n", i, PIN_LIMIT_SW, lvl,
               lvl == 0 ? "<-- TRIGGERED" : "open");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    printf("  PASS if pin reads 1 at rest and 0 when switch is activated.\n");
}

// ─── TEST 9: Display (ST7789) ────────────────────────────────────────────────

static esp_lcd_panel_handle_t    s_panel  = nullptr;
static esp_lcd_panel_io_handle_t s_lcd_io = nullptr;
// Global row buffer in internal RAM — safe for DMA transfers.
static uint16_t s_disp_row[DISP_WIDTH];

// bgr: use BGR element order instead of RGB
static bool display_ready(bool bgr) {
    if (s_panel) return true;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_DISP_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_DISP_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISP_WIDTH * sizeof(uint16_t),
    };
    esp_err_t r;
    r = spi_bus_initialize(DISP_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    printf("  spi_bus_initialize:       %s\n", esp_err_to_name(r));
    if (r != ESP_OK) return false;

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num          = PIN_DISP_CS,
        .dc_gpio_num          = PIN_DISP_DC,
        .spi_mode             = 0,
        .pclk_hz              = DISP_SPI_FREQ_HZ,
        .trans_queue_depth    = 4,
        .on_color_trans_done  = nullptr,
        .lcd_cmd_bits         = 8,
        .lcd_param_bits       = 8,
    };
    r = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST,
                                  &io_cfg, &s_lcd_io);
    printf("  esp_lcd_new_panel_io_spi: %s\n", esp_err_to_name(r));
    if (r != ESP_OK) return false;

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_DISP_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .flags          = { .reset_active_high = 0 },
        .vendor_config  = nullptr,
    };
    r = esp_lcd_new_panel_st7789(s_lcd_io, &panel_cfg, &s_panel);
    printf("  esp_lcd_new_panel_st7789: %s  bgr=%d\n", esp_err_to_name(r), bgr);
    if (r != ESP_OK) return false;

    esp_lcd_panel_reset(s_panel);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_io_tx_param(s_lcd_io, 0xB0, (uint8_t[]){0x00, 0xE8}, 2);
    esp_lcd_panel_swap_xy(s_panel, true);

    // 2. Fix Mirrored/Upside-down rendering if it occurs
    // Flip these true/false arguments if your text or images appear backwards
    esp_lcd_panel_mirror(s_panel, true, false);

    // 3. Fix the color space inversion (Teal -> Red, Yellow -> Blue, etc.)
    // esp_lcd_panel_invert_color(s_panel, true);

    // 4. Handle Hardware Display Gaps
    // If your image is shifted or has a line of static on the edge,
    // change these offsets (commonly 0, 0 or 0, 32 for certain ST7789 panels)
    esp_lcd_panel_set_gap(s_panel, 0, 0);

    // ─── END OF CONFIGURATION ───

    esp_lcd_panel_disp_on_off(s_panel, true);
    printf("  Panel ready.\n");
    return true;
}

// Destroy and recreate panel so diag test can change settings from scratch.
static void display_reset_panel() {
    if (s_panel) {
        // esp_lcd_del_panel omitted — null the handle so display_ready() re-inits
        s_panel = nullptr;
    }
    if (s_lcd_io) { esp_lcd_panel_io_del(s_lcd_io); s_lcd_io = nullptr; }
    spi_bus_free(DISP_SPI_HOST);
}

// byte_swap: swap each pixel's two bytes — fixes "Red→Yellow" colour corruption
//            caused by ESP32 DMA sending uint16_t LSB-first to an MSB-first display.
static void disp_fill(uint16_t color565, bool byte_swap = false) {
    uint16_t c = byte_swap ? __builtin_bswap16(color565) : color565;
    for (int x = 0; x < DISP_WIDTH; x++) s_disp_row[x] = c;
    for (int y = 0; y < DISP_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + 1, s_disp_row);
    }
}

// Drive a GPIO HIGH as a candidate backlight pin.
static void try_backlight(int gpio_num) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)gpio_num, 1);
}

// Reinit panel with specific settings, fill R/G/B for 1s each.
// byte_swap: swap the two bytes of each pixel in software (__builtin_bswap16)
static void diag_try(bool byte_swap, bool bgr, bool swap_xy,
                     bool invert, bool mirror_x, bool mirror_y,
                     const char* label) {
    printf("\n  [%s]\n", label);
    display_reset_panel();
    if (!display_ready(bgr)) return;
    esp_lcd_panel_set_gap(s_panel, 0, 0);
    esp_lcd_panel_invert_color(s_panel, invert);
    esp_lcd_panel_swap_xy(s_panel, swap_xy);
    esp_lcd_panel_mirror(s_panel, mirror_x, mirror_y);

    struct { uint16_t c; const char* n; } fills[] = {
        { 0xF800, "RED"   },
        { 0x07E0, "GREEN" },
        { 0x001F, "BLUE"  },
    };
    for (auto& f : fills) {
        printf("    %s\n", f.n);
        disp_fill(f.c, byte_swap);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void test_display_diag() {
    printf("\n=== TEST d: Display Diagnostic ===\n");
    printf("  Two known issues with ST7789 on ESP32:\n");
    printf("  1. Wrong colours (Red→Yellow etc): fix = swap_bytes=true\n");
    printf("  2. Screen not full: fix = swap_xy=true\n\n");
    printf("  Cycling 6 combinations. Note which shows correct R/G/B fills\n");
    printf("  AND fills the whole %dx%d screen.\n\n", DISP_WIDTH, DISP_HEIGHT);

    // swap_bytes | bgr   | swap_xy | invert | mirror_x | mirror_y
    diag_try(false, false, false, true,  true,  false, "A: baseline (swap_bytes=off, swap_xy=off)");
    diag_try(true,  false, false, true,  true,  false, "B: swap_bytes=on,  swap_xy=off");
    diag_try(false, false, true,  true,  true,  false, "C: swap_bytes=off, swap_xy=on");
    diag_try(true,  false, true,  true,  true,  false, "D: swap_bytes=on,  swap_xy=on  [most likely correct]");
    diag_try(true,  true,  true,  true,  true,  false, "E: swap_bytes=on,  swap_xy=on,  bgr=on");
    diag_try(true,  false, true,  false, true,  false, "F: swap_bytes=on,  swap_xy=on,  invert=off");

    display_reset_panel();
    printf("\n  Report which label (A-F) showed correct RED/GREEN/BLUE fills\n");
    printf("  filling the whole screen. Apply those settings in display_manager.cpp.\n");
}

static void test_display() {
    printf("\n=== TEST 9: Display (ST7789 %dx%d) ===\n", DISP_WIDTH, DISP_HEIGHT);
    printf("  MOSI=GPIO%d CLK=GPIO%d CS=GPIO%d DC=GPIO%d RST=GPIO%d\n",
           PIN_DISP_MOSI, PIN_DISP_CLK, PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST);
    if (!display_ready(true)) return;

    // --- Solid colour fills ---
    struct { uint16_t c; const char* name; } fills[] = {
        { 0xF800, "RED"     },
        { 0x07E0, "GREEN"   },
        { 0x001F, "BLUE"    },
        { 0xFFFF, "WHITE"   },
        { 0x0000, "BLACK"   },
    };
    for (auto& f : fills) {
        printf("  %s\n", f.name);
        disp_fill(f.c);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // --- Colour bars (8 horizontal bands) ---
    printf("  Colour bars (R / Y / G / C / B / M / W / K)...\n");
    const uint16_t bars[] = { 0xF800, 0xFFE0, 0x07E0, 0x07FF,
                               0x001F, 0xF81F, 0xFFFF, 0x0000 };
    const int band = DISP_HEIGHT / 8;
    for (int y = 0; y < DISP_HEIGHT; y++) {
        uint16_t c = bars[y / band < 8 ? y / band : 7];
        for (int x = 0; x < DISP_WIDTH; x++) s_disp_row[x] = c;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + 1, s_disp_row);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    // --- Checkerboard (8×8 px squares) ---
    printf("  Checkerboard (8px squares)...\n");
    for (int y = 0; y < DISP_HEIGHT; y++) {
        for (int x = 0; x < DISP_WIDTH; x++) {
            s_disp_row[x] = (((x >> 3) ^ (y >> 3)) & 1) ? 0xFFFF : 0x0000;
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + 1, s_disp_row);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    disp_fill(0x0000); // leave screen black
    printf("  PASS if all patterns displayed correctly.\n");
    printf("  Wrong colours?  Toggle rgb_ele_order BGR<->RGB in display_ready().\n");
    printf("  Flipped/rotated? Adjust swap_xy / mirror settings in display_ready().\n");
}

// ─── menu ────────────────────────────────────────────────────────────────────

static void print_menu() {
    printf("\n========= MoTrotten HW Test =========\n");
    printf(" 1  EN GPIO toggle (no motor movement)\n");
    printf(" 2  PWM output verification (scope)\n");
    printf(" 3  Motor run UP   2s  [CAUTION: moves]\n");
    printf(" 4  Motor run DOWN 2s  [CAUTION: moves]\n");
    printf(" 5  Current sense ADC readings\n");
    printf(" 6  Button ADC ladder\n");
    printf(" 7  VL53L0X I2C scan + model ID\n");
    printf(" t  VL53L0X range readings (library, 30 samples)\n");
    printf(" 8  Limit switch GPIO\n");
    printf(" 9  Display: solid fills + colour bars + checkerboard\n");
    printf(" d  Display diagnostic: gap/invert sweep (run this first)\n");
    printf(" m  Show this menu\n");
    printf("=====================================\n> ");
    fflush(stdout);
}

extern "C" void app_main() {
    printf("\n\nMoTrotten Hardware Test Firmware\n");
    printf("IDF: %s  Build: %s %s\n\n", IDF_VER, __DATE__, __TIME__);
    print_menu();

    while (true) {
        int c = getchar();
        if (c < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (c == '\n' || c == '\r') continue;
        printf("%c\n", c);
        switch (c) {
            case '1': test_gpio();          break;
            case '2': test_pwm();           break;
            case '3': test_motor(true);     break;
            case '4': test_motor(false);    break;
            case '5': test_current_sense(); break;
            case '6': test_buttons();       break;
            case '7': test_vl53l0x();       break;
            case 't': case 'T': test_tof_range(); break;
            case '8': test_limit_switch();  break;
            case '9': test_display();       break;
            case 'd': case 'D': test_display_diag(); break;
            case 'm': case 'M': break;
            default:
                printf("  Unknown key '%c'\n", c);
        }
        print_menu();
    }
}
