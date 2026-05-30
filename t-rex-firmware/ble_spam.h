#ifndef BLE_SPAM_H
#define BLE_SPAM_H

// blespam / bs — BLE notification spam suite
//   bs [apple]   — Apple Nearby Action (iOS pairing popups)
//   bs ms        — Microsoft Swift Pair (Windows pairing popup)
//   bs samsung   — Samsung Galaxy manufacturer data flood
//   bs all       — cycle all vendors
//
// Advertisement byte arrays from BruceDevices/firmware (AGPL-3.0)
// https://github.com/BruceDevices/firmware/tree/main/src/modules/ble

class BleSpam {
public:
    void command(const char* args);

private:
    void spamApple();
    void spamAndroid();
    void spamMicrosoft();
    void spamSamsung();
    void spamAll();
};

extern BleSpam bleSpam;

#endif // BLE_SPAM_H
