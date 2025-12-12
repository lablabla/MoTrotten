idf_component_register(SRCS 
  "main.cpp"

   INCLUDE_DIRS "." REQUIRES nvs_flash driver ina219 i2c vl53l logger)

