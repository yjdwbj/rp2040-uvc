cmake_minimum_required(VERSION 3.17)


set(FAMILY rp2040)
set(BOARD pico_sdk)

# set(CMAKE_VERBOSE_MAKEFILE ON)
add_compile_options(-Wno-error=unused-value
					-Wno-error=sign-conversion
					-Wno-error=conversion
					-Wno-error=unused-parameter
					-Wno-error=unused-variable)


set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# set(CFG_TUSB_OS OPT_OS_FREERTOS)
add_definitions(-DCONFIG_OV2640_SUPPORT=1)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)
include(${CMAKE_CURRENT_LIST_DIR}/pico-sdk/external/pico_sdk_import.cmake)


# set(USE_FREERTOS 1)

if (USE_FREERTOS)
	message("Build with FreeRTOS support")
	set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/freertos)
	include(FreeRTOS_Kernel_import.cmake)

	add_definitions(-DUSE_FREERTOS=1)
endif()


set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
include(${PICO_TINYUSB_PATH}/hw/bsp/rp2040/pico_sdk_import.cmake)
include(${PICO_TINYUSB_PATH}/hw/bsp/rp2040/family.cmake)

set(PROJECT pico-ov7670-uvc)
project(${PROJECT} C CXX ASM)

# family_initialize_project(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR})
pico_sdk_init()
set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

add_executable(${PROJECT})

pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/image.pio)
pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/ili9341_lcd.pio)


target_sources(${PROJECT} PUBLIC
#   ${CMAKE_CURRENT_SOURCE_DIR}/freertos_hook.c
  ${CMAKE_CURRENT_SOURCE_DIR}/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ov2640.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ili9341_lcd.c
  ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
)

target_include_directories(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOSConfig
)

if (NOT USE_FREERTOS)
target_link_libraries(${PROJECT} PUBLIC
	pico_stdlib
	hardware_i2c
	tinyusb_device
    tinyusb_board
	hardware_pio
	hardware_dma
)

pico_add_extra_outputs(${PROJECT})
family_configure_device_example(${PROJECT} noos)
else()
target_link_libraries(${PROJECT} PUBLIC
	pico_stdlib
	hardware_i2c
	tinyusb_device
    tinyusb_board
	hardware_pio
	hardware_dma
	FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap1
)

pico_add_extra_outputs(${PROJECT})
family_configure_device_example(${PROJECT} freertos)
endif()
