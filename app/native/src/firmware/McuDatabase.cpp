#include "trinity/firmware/McuDatabase.hpp"

#include "trinity/core/Error.hpp"

namespace trinity::firmware {

namespace {

McuPin gpioPin(const std::string& name, bool adc = false, bool pwm = true) {
    McuPin pin;
    pin.name = name;
    pin.digital = true;
    pin.adc = adc;
    pin.pwm = pwm;
    return pin;
}

PeripheralInfo peri(const std::string& name, const std::string& kind, int instances = 1) {
    PeripheralInfo out;
    out.name = name;
    out.kind = kind;
    out.instances = instances;
    return out;
}

MCU esp32() {
    MCU mcu;
    mcu.family = "ESP32";
    mcu.manufacturer = "Espressif";
    mcu.model = "ESP32";
    mcu.architecture = "xtensa-lx6";
    mcu.clockHzMax = 240000000LL;
    mcu.toolchain = "xtensa-esp32-elf-g++";
    for (int i = 0; i <= 33; ++i) {
        const bool adc = (i == 32 || i == 33);
        const bool pwmOk = (i < 34);
        mcu.pins.push_back(gpioPin("GPIO" + std::to_string(i), adc, pwmOk));
    }
    // Input-only ADC strapping companions.
    for (int i : {34, 35, 36, 39}) {
        McuPin pin;
        pin.name = "GPIO" + std::to_string(i);
        pin.digital = true;
        pin.adc = true;
        pin.pwm = false;
        mcu.pins.push_back(pin);
    }
    mcu.peripherals = {peri("UART0", "uart"), peri("UART1", "uart"),
                       peri("UART2", "uart"), peri("I2C0", "i2c"),
                       peri("I2C1", "i2c"),   peri("SPI0", "spi"),
                       peri("SPI1", "spi"),   peri("PWM", "pwm", 16)};
    return mcu;
}

MCU stm32f401re() {
    MCU mcu;
    mcu.family = "STM32F4";
    mcu.manufacturer = "STMicroelectronics";
    mcu.model = "STM32F401RE";
    mcu.architecture = "cortex-m4";
    mcu.clockHzMax = 84000000LL;
    mcu.toolchain = "arm-none-eabi-g++";
    for (const std::string& port : {"PA", "PB"}) {
        for (int i = 0; i <= 15; ++i) {
            const bool adc = (port == "PA" && i <= 7) || (port == "PB" && i <= 1);
            mcu.pins.push_back(gpioPin(port + std::to_string(i), adc, true));
        }
    }
    mcu.pins.push_back(gpioPin("PC13", false, false));
    mcu.peripherals = {peri("USART1", "uart"), peri("USART2", "uart"),
                       peri("USART6", "uart"), peri("I2C1", "i2c"),
                       peri("I2C2", "i2c"),   peri("I2C3", "i2c"),
                       peri("SPI1", "spi"),   peri("SPI2", "spi"),
                       peri("PWM", "pwm", 11)};
    return mcu;
}

MCU rp2040() {
    MCU mcu;
    mcu.family = "RP2040";
    mcu.manufacturer = "Raspberry Pi";
    mcu.model = "RP2040";
    mcu.architecture = "cortex-m0plus";
    mcu.clockHzMax = 133000000LL;
    mcu.toolchain = "arm-none-eabi-g++";
    for (int i = 0; i <= 29; ++i) {
        const bool adc = (i >= 26 && i <= 28);
        mcu.pins.push_back(gpioPin("GP" + std::to_string(i), adc, true));
    }
    mcu.peripherals = {peri("UART0", "uart"), peri("UART1", "uart"),
                       peri("I2C0", "i2c"),   peri("I2C1", "i2c"),
                       peri("SPI0", "spi"),   peri("SPI1", "spi"),
                       peri("PWM", "pwm", 8)};
    return mcu;
}

}  // namespace

std::vector<std::string> supportedMcus() {
    return {"ESP32", "STM32F401RE", "RP2040"};
}

MCU lookupMcu(const std::string& model) {
    if (model == "ESP32") {
        return esp32();
    }
    if (model == "STM32F401RE") {
        return stm32f401re();
    }
    if (model == "RP2040") {
        return rp2040();
    }
    core::Json supported = core::Json::array();
    for (const auto& name : supportedMcus()) {
        supported.push_back(name);
    }
    throw core::RequestValidationError("Unsupported MCU '" + model + "'",
                                       {{"supported", supported}}, "engines");
}

}  // namespace trinity::firmware
