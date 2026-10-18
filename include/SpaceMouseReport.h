#pragma once

#include <stdint.h>

// SpaceMouse Pro Wireless Bluetooth Edition
static constexpr uint8_t kVendorIdSource = 0x02;
static constexpr uint16_t kVendorId = 0x256F;
static constexpr uint16_t kProductId = 0xC638;
static constexpr uint16_t kProductVersion = 0x0110;

static constexpr int16_t kAxisMin = -350;
static constexpr int16_t kAxisMax = 350;

static constexpr uint8_t kReportAxis = 0x01;
static constexpr uint8_t kReportButtons = 0x03;
static constexpr uint8_t kReportLed = 0x04;

struct AxisReport {
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t rx;
  int16_t ry;
  int16_t rz;
} __attribute__((packed));

static uint8_t kReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x08,        // Usage (Multi-axis Controller)
    0xA1, 0x01,        // Collection (Application)
    0xA1, 0x00,        //   Collection (Physical)
    0x85, kReportAxis, //     Report ID (1)
    0x16, 0xA2, 0xFE,  //     Logical Minimum (-350)
    0x26, 0x5E, 0x01,  //     Logical Maximum (350)
    0x36, 0x88, 0xFA,  //     Physical Minimum (-1400)
    0x46, 0x78, 0x05,  //     Physical Maximum (1400)
    0x55, 0x0C,        //     Unit Exponent (-4)
    0x65, 0x11,        //     Unit (SI Linear, Centimeter)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x32,        //     Usage (Z)
    0x09, 0x33,        //     Usage (Rx)
    0x09, 0x34,        //     Usage (Ry)
    0x09, 0x35,        //     Usage (Rz)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x06,        //     Report Count (6)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0xC0,              //   End Collection
    0xA1, 0x00,        //   Collection (Physical)
    0x85, kReportButtons,
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x20,        //     Report Count (32)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x20,        //     Usage Maximum (Button 32)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0xC0,              //   End Collection
    0xA1, 0x02,        //   Collection (Logical)
    0x85, kReportLed,  //     Report ID (4)
    0x05, 0x08,        //     Usage Page (LEDs)
    0x09, 0x4B,        //     Usage (Generic Indicator)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x01,        //     Report Size (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x07,        //     Report Size (7)
    0x91, 0x03,        //     Output (Const,Var,Abs)
    0xC0,              //   End Collection
    0xC0               // End Collection
};
