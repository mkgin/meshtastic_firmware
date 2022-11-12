#include "../configuration.h"
#include "../main.h"
#include <Wire.h>
#include "mesh/generated/telemetry.pb.h"

// AXP192 and AXP2101 have the same device address, we just need to identify it in Power.cpp
#ifndef XPOWERS_AXP192_AXP2101_ADDRESS
#define XPOWERS_AXP192_AXP2101_ADDRESS      0x34
#endif

#if HAS_WIRE

void printATECCInfo()
{
#ifndef ARCH_PORTDUINO
    atecc.readConfigZone(false);

    DEBUG_MSG("ATECC608B Serial Number: ");
    for (int i = 0 ; i < 9 ; i++) {
        DEBUG_MSG("%02x",atecc.serialNumber[i]);
    }

    DEBUG_MSG(", Rev Number: ");
    for (int i = 0 ; i < 4 ; i++) {
        DEBUG_MSG("%02x",atecc.revisionNumber[i]);
    }
    DEBUG_MSG("\n");

    DEBUG_MSG("ATECC608B Config %s",atecc.configLockStatus ? "Locked" : "Unlocked");
    DEBUG_MSG(", Data %s",atecc.dataOTPLockStatus ? "Locked" : "Unlocked");
    DEBUG_MSG(", Slot 0 %s\n",atecc.slot0LockStatus ? "Locked" : "Unlocked");

    if (atecc.configLockStatus && atecc.dataOTPLockStatus && atecc.slot0LockStatus) {
        if (atecc.generatePublicKey() == false) {
            DEBUG_MSG("ATECC608B Error generating public key\n");
        } else {
            DEBUG_MSG("ATECC608B Public Key: ");
            for (int i = 0 ; i < 64 ; i++) {
                DEBUG_MSG("%02x",atecc.publicKey64Bytes[i]);
            }
            DEBUG_MSG("\n");
        }
    }
#endif
}

uint16_t getRegisterValue(uint8_t address, uint8_t reg, uint8_t length, TwoWire myWire) {
    uint16_t value = 0x00;
    myWire.beginTransmission(address);
    myWire.write(reg);
    myWire.endTransmission();
    delay(20);
    myWire.requestFrom(address, length);
    DEBUG_MSG("Wire.available() = %d\n", myWire.available());
    if (myWire.available() == 2) {
        // Read MSB, then LSB
        value = (uint16_t)myWire.read() << 8;  
        value |= myWire.read();
    } else if (myWire.available()) {
        value = myWire.read();
    }
    return value;
}

uint8_t oled_probe(byte addr, TwoWire myWire)
{
    uint8_t r = 0;
    uint8_t r_prev = 0;
    uint8_t c = 0;
    uint8_t o_probe = 0;
    do {
        r_prev = r;
        myWire.beginTransmission(addr);
        myWire.write(0x00);
        myWire.endTransmission();
        myWire.requestFrom((int)addr, 1);
        if (myWire.available()) {
            r = myWire.read();
        }
        r &= 0x0f;

        if (r == 0x08 || r == 0x00) {
            o_probe = 2; // SH1106
        } else if ( r == 0x03 || r == 0x04 || r == 0x06 || r == 0x07) {
            o_probe = 1; // SSD1306
        }
        c++;
    } while ((r != r_prev) && (c < 4));
    DEBUG_MSG("0x%x subtype probed in %i tries \n", r, c);
    return o_probe;
}

void scanI2Cdevice(TwoWire myWire, uint8_t busnum)
{
    byte err, addr;
    uint16_t registerValue = 0x00;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        myWire.beginTransmission(addr);
        err = myWire.endTransmission();
        if (err == 0) {
            DEBUG_MSG("I2C device found at address 0x%x\n", addr);

            i2cScanMap[addr] = {addr, busnum};

            nDevices++;

            if (addr == SSD1306_ADDRESS) {
                screen_found = addr;
                screen_model = oled_probe(addr, myWire);
                if (screen_model == 1) {
                    DEBUG_MSG("ssd1306 display found\n");
                } else if (screen_model == 2) {
                    DEBUG_MSG("sh1106 display found\n");
                } else {
                    DEBUG_MSG("unknown display found\n");
                }
            }
#ifndef ARCH_PORTDUINO
            if (addr == ATECC608B_ADDR) {
                if (atecc.begin(ATECC608B_ADDR) == true) {
                    DEBUG_MSG("ATECC608B initialized\n");
                } else {
                    DEBUG_MSG("ATECC608B initialization failed\n");
                }
                printATECCInfo();
            }
#endif
#ifdef RV3028_RTC
            if (addr == RV3028_RTC){
                DEBUG_MSG("RV3028 RTC found\n");
                Melopero_RV3028 rtc;
                rtc.initI2C(myWire);
                rtc.writeToRegister(0x35,0x07); // no Clkout
                rtc.writeToRegister(0x37,0xB4);
            }
#endif
#ifdef PCF8563_RTC
            if (addr == PCF8563_RTC){
                DEBUG_MSG("PCF8563 RTC found\n");
            }
#endif
            if (addr == CARDKB_ADDR) {
                // Do we have the RAK14006 instead?
                registerValue = getRegisterValue(addr, 0x04, 1, myWire);
                if (registerValue == 0x02) { // KEYPAD_VERSION
                    DEBUG_MSG("RAK14004 found\n");
                    kb_model = 0x02;
                } else {
                    DEBUG_MSG("m5 cardKB found\n");
                    kb_model = 0x00;
                }
            }
            if (addr == ST7567_ADDRESS) {
                screen_found = addr;
                DEBUG_MSG("st7567 display found\n");
            }
#ifdef HAS_PMU
            if (addr == XPOWERS_AXP192_AXP2101_ADDRESS) {
                DEBUG_MSG("axp192/axp2101 PMU found\n");
            }
#endif
            if (addr == BME_ADDR || addr == BME_ADDR_ALTERNATE) {
                registerValue = getRegisterValue(addr, 0xD0, 1, myWire); // GET_ID
                if (registerValue == 0x61) {
                    DEBUG_MSG("BME-680 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BME680] = addr;
                } else if (registerValue == 0x60) {
                    DEBUG_MSG("BME-280 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BME280] = addr;
                } else {
                    DEBUG_MSG("BMP-280 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BMP280] = addr;
                }
            }
            if (addr == INA_ADDR || addr == INA_ADDR_ALTERNATE) {
                registerValue = getRegisterValue(addr, 0xFE, 2, myWire);
                DEBUG_MSG("Register MFG_UID: 0x%x\n", registerValue);
                if (registerValue == 0x5449) {
                    DEBUG_MSG("INA260 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_INA260] = addr;
                } else { // Assume INA219 if INA260 ID is not found
                    DEBUG_MSG("INA219 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_INA219] = addr;
                }
            }
            if (addr == MCP9808_ADDR) {
                nodeTelemetrySensorsMap[TelemetrySensorType_MCP9808] = addr;
                DEBUG_MSG("MCP9808 sensor found\n");
            }
            if (addr == SHTC3_ADDR) {
                DEBUG_MSG("SHTC3 sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_SHTC3] = addr;
            }
            if (addr == LPS22HB_ADDR || addr == LPS22HB_ADDR_ALT) {
                DEBUG_MSG("LPS22HB sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_LPS22] = addr;
            }

            // High rate sensors, will be processed internally
            if (addr == QMC6310_ADDR) {
                DEBUG_MSG("QMC6310 Highrate 3-Axis magnetic sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMC6310] = addr;
            }
            if (addr == QMI8658_ADDR) {
                DEBUG_MSG("QMI8658 Highrate 6-Axis inertial measurement sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMI8658] = addr;
            }
            if (addr == QMC5883L_ADDR) {
                DEBUG_MSG("QMC5883L Highrate 3-Axis magnetic sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMC5883L] = addr;
            }
        } else if (err == 4) {
            DEBUG_MSG("Unknow error at address 0x%x\n", addr);
        }
    }

    if (nDevices == 0)
        DEBUG_MSG("No I2C devices found\n");
    else
        DEBUG_MSG("%i I2C devices found\n",nDevices);
}
#else
void scanI2Cdevice(TwoWire myWire, uint8_t busnum) {}
#endif
