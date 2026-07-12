cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT DEFINED PICO_SDK_PATH)
  message(FATAL_ERROR "PICO_SDK_PATH tanimli degil. GitHub Actions bunu otomatik verir.")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(GT_SUPER_KONTROLER_2026_V001F_STABLE C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

include_directories(${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/include)

function(add_gt_mouse_target target player_num pid_suffix product_name)
  add_executable(${target}
    src/mouse_main.c
    src/usb_descriptors.c
  )
  target_compile_definitions(${target} PRIVATE
    USB_PROFILE_MOUSE=1
    PLAYER_NUM=${player_num}
    USB_PID=0x40${pid_suffix}
    USB_PRODUCT_STRING="${product_name}"
  )
  target_link_libraries(${target}
    pico_stdlib
    hardware_adc
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
  )
  pico_add_extra_outputs(${target})
endfunction()

function(add_gt_controller_target target)
  add_executable(${target}
    src/controller_main.c
    src/usb_descriptors.c
  )
  target_compile_definitions(${target} PRIVATE
    USB_PROFILE_KEYBOARD=1
    USB_PID=0x4101
    USB_PRODUCT_STRING="GT_SUPER KONTROLER 2026 V001F CONTROLLER"
  )
  target_link_libraries(${target}
    pico_stdlib
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
  )
  pico_add_extra_outputs(${target})
endfunction()

add_gt_mouse_target(piko1 1 31 "GT_SUPER KONTROLER 2026 V001F P1 ABS MOUSE")
add_gt_mouse_target(piko2 2 32 "GT_SUPER KONTROLER 2026 V001F P2 ABS MOUSE")
add_gt_controller_target(controller)
