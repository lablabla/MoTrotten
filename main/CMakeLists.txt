idf_component_register(SRCS 
  "main.cpp"
  "motor_driver.cpp"
  "display_manager.cpp"
  "ui_manager.cpp"
  "VL53L0X/VL53L0X.cpp"

   INCLUDE_DIRS "." REQUIRES nvs_flash driver ina219 i2c vl53l logger lvgl esp_lcd)

