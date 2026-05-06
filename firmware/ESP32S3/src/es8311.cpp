/**
 * es8311.cpp - Driver I2C danh thuc chip am thanh ES8311
 * Board: Xiaozhi S3 (ES3N28P)
 *
 * Clock strategy: Dung BCLK thay vi MCLK (don gian hon, khong can IO4)
 *   BCLK = FS * 32 = 16000 * 32 = 512 kHz
 *   Dung dang: I2S Standard, 16-bit, Mono, Slave
 *
 * Tham khao: ES8311 Datasheet, Espressif esp-idf es8311 component
 */

#include "es8311.h"
#include <Arduino.h>
#include <Wire.h>

#define ES8311_ADDR 0x18

static bool es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

static uint8_t es_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// Kiem tra nhanh: chip co dang phan hoi tren I2C khong?
// Tra ve true = chip dang song, false = mat ket noi hoac sai dia chi
bool es8311_check() {
    Wire.beginTransmission(ES8311_ADDR);
    return (Wire.endTransmission() == 0);
}

bool es8311_init() {
    // Kiem tra chip co tren bus I2C
    Wire.beginTransmission(ES8311_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        Serial.printf("  [ES8311] LOI: Khong tim thay chip 0x%02X (I2C err=%d)\n",
                      ES8311_ADDR, err);
        Serial.println("  [ES8311] Kiem tra: SDA=IO16, SCL=IO15, pull-up 4.7k");
        return false;
    }
    Serial.printf("  [ES8311] Tim thay chip tai 0x%02X\n", ES8311_ADDR);

    // 1. RESET toan bo chip
    es_write(0x00, 0x1F);
    delay(20);
    es_write(0x00, 0x00);
    delay(10);

    // 2. Clock: Dung BCLK thay vi MCLK
    //    Register 0x01, bit7 = 1 -> dung BCLK lam clock nguon
    //    BCLK = 512kHz (= FS*32 = 16000*32)
    //    Chip tu sinh LRCK tu BCLK/32
    es_write(0x01, 0x80);   // BCLK = clock source (bit7=1)
    es_write(0x02, 0x00);   // Pre-divider = /1
    es_write(0x03, 0x10);   // ADC clock
    es_write(0x04, 0x10);   // ADC LRCK divider
    es_write(0x05, 0x10);   // DAC LRCK divider

    // 3. System power
    es_write(0x0D, 0x01);   // Sysclk enable
    es_write(0x0E, 0x02);   // DAC clock on

    // 4. I2S format: Slave, Standard I2S, 16-bit
    es_write(0x0F, 0x44);   // Slave mode, I2S standard, 16-bit
    es_write(0x10, 0x0C);   // ADC I2S
    es_write(0x11, 0x0C);   // DAC I2S

    // 5. Analog power
    es_write(0x13, 0x10);   // VMID reference on
    es_write(0x14, 0x1A);   // MIC bias on
    delay(10);

    // 6. ADC (Mic) - Gain 24dB
    es_write(0x15, 0x00);   // ADC power on
    es_write(0x16, 0x24);   // PGA Gain = 24dB
    es_write(0x17, 0xBF);   // ADC Digital Volume = 0dB
    es_write(0x1A, 0xA0);   // ADC HPF on (loc nhieu DC)

    // 7. DAC (Loa)
    es_write(0x31, 0x00);   // DAC Unmute (bit0=0: khong tat tieng)
    es_write(0x32, 0xBF);   // DAC Volume = 0dB
    es_write(0x37, 0x08);   // Headphone amp on
    es_write(0x38, 0x08);   // HP output enable
    delay(20);

    Serial.println("  [ES8311] Kich hoat OK — DAC/ADC san sang, Volume 0dB");
    return true;
}
