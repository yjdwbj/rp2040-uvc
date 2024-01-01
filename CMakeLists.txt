cmake_minimum_required(VERSION 3.17)
set(PROJECT pico-ov7670-uvc)

set(FAMILY rp2040)
set(BOARD pico_sdk)

# set(CMAKE_VERBOSE_MAKEFILE ON)
add_compile_options(-Wno-error=unused-value
					-Wno-error=sign-conversion
					-Wno-error=conversion
					-Wno-error=unused-parameter
					-Wno-error=unused-variable)


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CFG_TUSB_OS OPT_OS_FREERTOS)
add_definitions(-DCONFIG_OV2640_SUPPORT=1)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)
set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/tinyusb)

include(${PICO_TINYUSB_PATH}/hw/bsp/family_support.cmake)
project(${PROJECT} C CXX ASM)

family_initialize_project(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(${PROJECT})

pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/image.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/ili9341_lcd.pio)


target_sources(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ov2640.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ili9341_lcd.c
#   ${CMAKE_CURRENT_SOURCE_DIR}/sensor.c
#   ${CMAKE_CURRENT_SOURCE_DIR}/camera.c
  ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
)

target_include_directories(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT} PUBLIC
	pico_stdlib
	hardware_i2c
	hardware_pio
	hardware_dma
	hardware_pwm
)

pico_add_extra_outputs(${PROJECT})
family_configure_device_example(${PROJECT} noos)
