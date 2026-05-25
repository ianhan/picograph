#pragma once

#include <stdint.h>

namespace picograph {

struct UsbKeyboardState {
    uint8_t modifier;
    uint8_t keycode[6];
};

struct UsbMouseState {
    bool event;
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
};

struct UsbJoystickState {
    uint8_t joy1_x;
    uint8_t joy1_y;
    uint8_t joy2_x;
    uint8_t joy2_y;
    uint8_t button_mask;
};

struct UsbXinputState {
    bool connected;
    uint16_t buttons;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
};

struct UsbHostState {
    bool enabled;
    uint8_t mounted_devices;
    uint8_t mounted_hid_interfaces;
    UsbKeyboardState keyboard;
    UsbMouseState mouse;
    UsbJoystickState joystick;
    UsbXinputState xinput;
    char device_message[4][80];
};

bool usb_host_start(bool serial_usb_active);
void usb_host_stop();
void usb_host_task();
const UsbHostState &usb_host_state();

}  // namespace picograph
