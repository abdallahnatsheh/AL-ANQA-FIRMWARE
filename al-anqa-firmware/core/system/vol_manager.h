#pragma once
#include <Arduino.h>

uint8_t getMasterVolume();
void    setMasterVolume(uint8_t v);   // set + save to SD
void    loadVolConf();                // call once from setup() after SD init
void    volCmd(char* args);           // `vol` / `volume` command handler
