#ifndef FAST_PAIR_KEYS_H
#define FAST_PAIR_KEYS_H

#include <stdint.h>

struct FpKnownDevice {
    uint32_t    modelId;
    const char* name;
};

// Model IDs sourced from public BLE scans + Google Fast Pair open registry.
// Used for display name resolution only — wrong entries just show hex model ID,
// no functional impact on the attack.
static const FpKnownDevice FP_KNOWN_DEVICES[] = {
    { 0x2A954B, "Pixel Buds A-Series"  },
    { 0x1E89A3, "Pixel Buds Pro"       },
    { 0x718FA4, "Pixel Buds (2019)"    },
    { 0x49426D, "Pixel Buds Pro 2"     },
    { 0xF52494, "Pixel Buds 2a"        },
    { 0xD446A7, "Galaxy Buds2"         },
    { 0x72EF8D, "Galaxy Buds Live"     },
    { 0x0356F0, "Galaxy Buds Pro"      },
    { 0xBFA9B3, "Galaxy Buds2 Pro"     },
    { 0x08E888, "WH-1000XM4"           },
    { 0x0D6867, "WF-1000XM4"           },
    { 0xCD8256, "WH-1000XM5"           },
    { 0x0A927B, "QuietComfort 45"      },
    { 0x2D7A23, "QC Earbuds II"        },
    { 0x92BBBD, "JBL Tune 230NC"       },
    { 0x4C94BE, "JBL Live Pro 2"       },
    { 0x821F66, "Jabra Evolve2 55"     },
    { 0x1B4F4B, "Jabra Elite 85h"      },
    { 0xCC4A22, "Soundcore Liberty 4"  },
    { 0xB5D5BF, "Surface Earbuds"      },
    { 0x06F739, "Moto Buds+"           },
};
static constexpr int FP_KNOWN_COUNT = (int)(sizeof(FP_KNOWN_DEVICES) / sizeof(FP_KNOWN_DEVICES[0]));

inline const char* fpLookupName(uint32_t modelId) {
    for (int i = 0; i < FP_KNOWN_COUNT; i++)
        if (FP_KNOWN_DEVICES[i].modelId == modelId) return FP_KNOWN_DEVICES[i].name;
    return nullptr;
}

#endif // FAST_PAIR_KEYS_H
