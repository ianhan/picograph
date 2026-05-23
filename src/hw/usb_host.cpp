#include "picomem/usb_host.h"

#include <stdio.h>
#include <string.h>

#include "tusb.h"

#if PICOMEM_ENABLE_XINPUT
#include "xinput_host.h"
#endif

#if PICOMEM_ENABLE_DISPLAYLINK
#include "libdlo.h"
#endif

namespace picomem {
namespace {

UsbHostState state;

const char *vendor_name(uint16_t vid) {
    switch (vid) {
    case 0x054c:
        return "Sony ";
    case 0x046d:
        return "Logitech ";
    case 0x03f0:
        return "HP ";
    case 0x17ef:
        return "Lenovo ";
    case 0x413c:
        return "Dell ";
    case 0x045e:
        return "Microsoft ";
    case 0x057e:
        return "Nintendo ";
    case 0x044f:
        return "Thrustmaster ";
    case 0x0738:
        return "Mad Catz ";
    case 0x06a3:
        return "Saitek ";
    case 0x2dc8:
        return "8BitDo ";
    case 0x146b:
        return "BigBen ";
    default:
        return "";
    }
}

void refresh_mount_count() {
    uint8_t count = 0;
    for (uint8_t dev_addr = 1; dev_addr <= CFG_TUH_DEVICE_MAX; ++dev_addr) {
        if (tuh_mounted(dev_addr)) {
            ++count;
        }
    }
    state.mounted_devices = count;
}

void set_status(uint8_t dev_addr, const char *message) {
    if (dev_addr == 0 || dev_addr > CFG_TUH_DEVICE_MAX) {
        return;
    }
    snprintf(state.device_message[dev_addr - 1],
             sizeof(state.device_message[dev_addr - 1]),
             "%u: %s",
             dev_addr,
             message);
    printf("usb host: %s\n", state.device_message[dev_addr - 1]);
}

#if PICOMEM_ENABLE_DISPLAYLINK
void handle_displaylink_mount(uint8_t dev_addr) {
    static bool initialized = false;

    if (!initialized) {
        dlo_init_t init_flags = {0};
        initialized = (dlo_init(init_flags) == dlo_ok);
    }
    if (!initialized || dlo_check_device(dev_addr) != dlo_ok) {
        return;
    }

    dlo_claim_t claim_flags = {0};
    dlo_dev_t uid = dlo_claim_first_device(claim_flags, 0);
    if (!uid) {
        return;
    }

    dlo_fill_rect(uid, nullptr, nullptr, DLO_RGB(0, 0, 0));
    dlo_flush_usb(uid, true);
    if (dlo_device_configured) {
        dlo_device_configured(uid);
    }
    set_status(dev_addr, "DisplayLink display");
}
#endif

#if PICOMEM_ENABLE_XINPUT
void update_joystick_from_xinput(uint16_t buttons,
                                 int16_t left_x,
                                 int16_t left_y,
                                 int16_t right_x,
                                 int16_t right_y,
                                 uint8_t left_trigger,
                                 uint8_t right_trigger) {
    uint8_t dpad = buttons & 0x0f;
    if (!dpad) {
        state.joystick.joy1_x = static_cast<uint8_t>((static_cast<int32_t>(left_x) + 32768) >> 8);
        state.joystick.joy1_y = static_cast<uint8_t>((-static_cast<int32_t>(left_y) + 32767) >> 8);
    } else {
        state.joystick.joy1_x = (dpad & XINPUT_GAMEPAD_DPAD_RIGHT) ? 255 :
                                ((dpad & XINPUT_GAMEPAD_DPAD_LEFT) ? 0 : 127);
        state.joystick.joy1_y = (dpad & XINPUT_GAMEPAD_DPAD_DOWN) ? 255 :
                                ((dpad & XINPUT_GAMEPAD_DPAD_UP) ? 0 : 127);
    }

    if (left_trigger) {
        state.joystick.joy1_y = 127u + (left_trigger >> 1);
    } else if (right_trigger) {
        state.joystick.joy1_y = 127u - (right_trigger >> 1);
    }

    state.joystick.joy2_x = static_cast<uint8_t>((static_cast<int32_t>(right_x) + 32768) >> 8);
    state.joystick.joy2_y = static_cast<uint8_t>((-static_cast<int32_t>(right_y) + 32767) >> 8);
    state.joystick.button_mask = static_cast<uint8_t>((~(buttons >> 12)) << 4);
}
#endif

}  // namespace

bool usb_host_start(bool serial_usb_active) {
    if (state.enabled) {
        return true;
    }

    if (serial_usb_active) {
        printf("usb host: not started because USB serial stdio is active\n");
        return false;
    }

    tuh_init(BOARD_TUH_RHPORT);
    state.enabled = true;
    printf("usb host: started on rhport %d\n", BOARD_TUH_RHPORT);
    return true;
}

void usb_host_stop() {
    state.enabled = false;
}

void usb_host_task() {
    if (!state.enabled) {
        return;
    }
    tuh_task();
}

const UsbHostState &usb_host_state() {
    return state;
}

}  // namespace picomem

extern "C" void tuh_mount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    picomem::refresh_mount_count();
#if PICOMEM_ENABLE_DISPLAYLINK
    picomem::handle_displaylink_mount(dev_addr);
#endif
}

extern "C" void tuh_umount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    picomem::refresh_mount_count();
}

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr,
                                  uint8_t instance,
                                  uint8_t const *report_desc,
                                  uint16_t desc_len) {
    (void)report_desc;
    (void)desc_len;
    ++picomem::state.mounted_hid_interfaces;

    bool has_keyboard = false;
    bool has_mouse = false;
    bool has_joystick = false;
    uint8_t other_reports = 0;
    for (uint8_t i = 0; i <= instance; ++i) {
        switch (tuh_hid_interface_protocol(dev_addr, i)) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            has_keyboard = true;
            break;
        case HID_ITF_PROTOCOL_MOUSE:
            has_mouse = true;
            break;
        case HID_ITF_PROTOCOL_NONE:
            ++other_reports;
            break;
        default:
            break;
        }
    }

    tuh_hid_report_info_t report_info[4];
    uint8_t report_count = 0;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_NONE) {
        report_count = tuh_hid_parse_report_descriptor(report_info, 4, report_desc, desc_len);
        for (uint8_t i = 0; i < report_count; ++i) {
            if (report_info[i].usage == HID_USAGE_DESKTOP_JOYSTICK ||
                report_info[i].usage == HID_USAGE_DESKTOP_GAMEPAD) {
                has_joystick = true;
            }
        }
    }

    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    char message[80];
    if (has_keyboard && has_mouse) {
        snprintf(message, sizeof(message), "%sUSB keyboard and mouse", picomem::vendor_name(vid));
    } else if (has_keyboard) {
        snprintf(message, sizeof(message), "%sUSB keyboard", picomem::vendor_name(vid));
    } else if (has_mouse) {
        snprintf(message, sizeof(message), "%sUSB mouse", picomem::vendor_name(vid));
    } else if (has_joystick) {
        snprintf(message, sizeof(message), "%sUSB joystick", picomem::vendor_name(vid));
    } else {
        snprintf(message, sizeof(message), "%sUSB %u report%s v:%04x p:%04x",
                 picomem::vendor_name(vid),
                 other_reports,
                 other_reports == 1 ? "" : "s",
                 vid,
                 pid);
    }
    picomem::set_status(dev_addr, message);

    if (!tuh_hid_receive_report(dev_addr, instance)) {
        printf("usb host: failed to request HID report dev=%u instance=%u\n", dev_addr, instance);
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
    if (picomem::state.mounted_hid_interfaces > 0) {
        --picomem::state.mounted_hid_interfaces;
    }
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr,
                                            uint8_t instance,
                                            uint8_t const *report,
                                            uint16_t len) {
    uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (protocol == HID_ITF_PROTOCOL_KEYBOARD && len >= sizeof(hid_keyboard_report_t)) {
        auto const *keyboard = reinterpret_cast<hid_keyboard_report_t const *>(report);
        picomem::state.keyboard.modifier = keyboard->modifier;
        memcpy(picomem::state.keyboard.keycode, keyboard->keycode, sizeof(picomem::state.keyboard.keycode));
    } else if (protocol == HID_ITF_PROTOCOL_MOUSE && len >= sizeof(hid_mouse_report_t)) {
        auto const *mouse = reinterpret_cast<hid_mouse_report_t const *>(report);
        picomem::state.mouse.event = true;
        picomem::state.mouse.buttons = mouse->buttons;
        picomem::state.mouse.x = mouse->x;
        picomem::state.mouse.y = mouse->y;
        picomem::state.mouse.wheel = mouse->wheel;
    }

    (void)tuh_hid_receive_report(dev_addr, instance);
}

#if PICOMEM_ENABLE_XINPUT
extern "C" usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &usbh_xinput_driver;
}

extern "C" void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, xinputh_interface_t const *xinput_itf) {
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    const char *type = "Unknown";
    switch (xinput_itf->type) {
    case XBOXONE:
        type = "Xbox One";
        break;
    case XBOX360_WIRELESS:
        type = "Xbox 360 Wireless";
        break;
    case XBOX360_WIRED:
        type = "Xbox 360 Wired";
        break;
    case XBOXOG:
        type = "Xbox OG";
        break;
    default:
        break;
    }

    char message[80];
    snprintf(message, sizeof(message), "%s%s Joystick (XInput)", picomem::vendor_name(vid), type);
    picomem::set_status(dev_addr, message);

    picomem::state.xinput.connected = true;
    if (xinput_itf->type == XBOX360_WIRELESS && !xinput_itf->connected) {
        (void)tuh_xinput_receive_report(dev_addr, instance);
        return;
    }
    (void)tuh_xinput_set_led(dev_addr, instance, 0, true);
    (void)tuh_xinput_set_led(dev_addr, instance, 1, true);
    (void)tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
    (void)tuh_xinput_receive_report(dev_addr, instance);
}

extern "C" void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
    picomem::state.xinput = {};
}

extern "C" void tuh_xinput_report_received_cb(uint8_t dev_addr,
                                               uint8_t instance,
                                               xinputh_interface_t const *xid_itf,
                                               uint16_t len) {
    (void)len;
    picomem::state.xinput.connected = xid_itf->connected;
    picomem::state.xinput.buttons = xid_itf->pad.wButtons;
    picomem::state.xinput.left_trigger = xid_itf->pad.bLeftTrigger;
    picomem::state.xinput.right_trigger = xid_itf->pad.bRightTrigger;
    picomem::state.xinput.left_x = xid_itf->pad.sThumbLX;
    picomem::state.xinput.left_y = xid_itf->pad.sThumbLY;
    picomem::state.xinput.right_x = xid_itf->pad.sThumbRX;
    picomem::state.xinput.right_y = xid_itf->pad.sThumbRY;
    if (xid_itf->last_xfer_result == XFER_RESULT_SUCCESS && xid_itf->connected && xid_itf->new_pad_data) {
        picomem::update_joystick_from_xinput(xid_itf->pad.wButtons,
                                             xid_itf->pad.sThumbLX,
                                             xid_itf->pad.sThumbLY,
                                             xid_itf->pad.sThumbRX,
                                             xid_itf->pad.sThumbRY,
                                             xid_itf->pad.bLeftTrigger,
                                             xid_itf->pad.bRightTrigger);
    }
    (void)tuh_xinput_receive_report(dev_addr, instance);
}
#endif
