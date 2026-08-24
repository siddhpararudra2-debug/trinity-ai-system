"""Explicit firmware target registry.

A target is intentionally concrete: code generation is allowed only for a
profile in this registry, so unsupported board/MCU combinations are rejected.
"""
from __future__ import annotations

from app.firmware.models import FirmwareTarget, TargetKind


TARGETS: list[FirmwareTarget] = [
    FirmwareTarget(
        id="esp32-devkitc-esp32-idf", name="ESP32 DevKitC", kind=TargetKind.mcu,
        vendor="Espressif", board="ESP32-DevKitC", mcu="ESP32", framework="ESP-IDF",
        language="c", build_system="CMake + idf.py", build_command="idf.py build", flash_command="idf.py -p PORT flash",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth"],
        notes=["Free ESP-IDF toolchain; verify exact module and GPIO strapping pins."]
    ),
    FirmwareTarget(
        id="arduino-uno-avr", name="Arduino Uno", kind=TargetKind.mcu,
        vendor="Arduino", board="Arduino Uno", mcu="ATmega328P", framework="Arduino AVR",
        language="cpp", build_system="PlatformIO", build_command="pio run", flash_command="pio run -t upload",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm"],
        notes=["Use a USB-connected Uno and confirm the serial port before flashing."]
    ),
    FirmwareTarget(
        id="stm32f411-blackpill-hal", name="STM32F411 Black Pill", kind=TargetKind.mcu,
        vendor="STMicroelectronics", board="Black Pill F411CEU6", mcu="STM32F411CEU6", framework="STM32 HAL",
        language="c", build_system="CMake + arm-none-eabi-gcc", build_command="cmake --build build", flash_command="st-flash write build/firmware.bin 0x8000000",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "timers", "dma"],
        notes=["Generated project requires the matching STM32Cube HAL/device package."]
    ),
    FirmwareTarget(
        id="rp2040-pico-sdk", name="Raspberry Pi Pico", kind=TargetKind.mcu,
        vendor="Raspberry Pi", board="Pico", mcu="RP2040", framework="Pico SDK",
        language="c", build_system="CMake + Pico SDK", build_command="cmake --build build", flash_command="picotool load build/firmware.uf2 -fx",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm", "pio", "usb"],
        notes=["Set PICO_SDK_PATH in the toolchain environment before building."]
    ),
    FirmwareTarget(
        id="nrf52840-dk-zephyr", name="nRF52840 DK", kind=TargetKind.mcu,
        vendor="Nordic Semiconductor", board="nRF52840 DK", mcu="nRF52840", framework="Zephyr",
        language="c", build_system="west + CMake", build_command="west build -b nrf52840dk_nrf52840", flash_command="west flash",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm", "ble", "usb"],
        notes=["Install Zephyr SDK and use the exact board name supported by the pinned Zephyr release."]
    ),
    FirmwareTarget(
        id="pixhawk-px4-fmu-v6c", name="Pixhawk FMUv6C", kind=TargetKind.flight_controller,
        vendor="Holybro/Pixhawk", board="Pixhawk FMUv6C", mcu="STM32H743", framework="PX4",
        language="cpp", build_system="PX4 + CMake", build_command="make px4_fmu-v6c_default", flash_command="make px4_fmu-v6c_default upload",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "barometer", "gps"],
        notes=["Generated code must integrate as a PX4 module or driver; do not bypass PX4 safety controls."]
    ),
    FirmwareTarget(
        id="pixhawk-ardupilot-cubeorange", name="Cube Orange", kind=TargetKind.flight_controller,
        vendor="CubePilot", board="Cube Orange", mcu="STM32H743", framework="ArduPilot",
        language="cpp", build_system="Waf", build_command="./waf configure --board CubeOrange && ./waf copter", flash_command="./waf copter --upload",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "barometer", "gps"],
        notes=["Use ArduPilot libraries and parameter conventions; test in SITL before hardware."]
    ),
    FirmwareTarget(
        id="betaflight-f4-generic", name="Betaflight STM32 F4 target", kind=TargetKind.flight_controller,
        vendor="Open flight-controller ecosystem", board="Generic STM32 F4 FC", mcu="STM32F405/F411", framework="Betaflight",
        language="c", build_system="Make", build_command="make TARGET=GENERIC_F4", flash_command="dfu-util -D obj/firmware.bin",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "pwm", "imu", "osd"],
        notes=["A real Betaflight target name and pin target file must be selected before flashing."]
    ),
    FirmwareTarget(
        id="inav-f7-generic", name="INAV STM32 F7 target", kind=TargetKind.flight_controller,
        vendor="Open flight-controller ecosystem", board="Generic STM32 F7 FC", mcu="STM32F722", framework="INAV",
        language="c", build_system="Make", build_command="make TARGET=GENERIC_F7", flash_command="dfu-util -D obj/firmware.bin",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "gps", "osd"],
        notes=["A real INAV target and board definition must be confirmed before flashing."]
    ),
]

TARGETS.extend([
    FirmwareTarget(
        id="esp32-s3-devkitc-esp-idf", name="ESP32-S3 DevKitC", kind=TargetKind.mcu,
        vendor="Espressif", board="ESP32-S3-DevKitC", mcu="ESP32-S3", framework="ESP-IDF",
        language="c", build_system="CMake + idf.py", build_command="idf.py build", flash_command="idf.py -p PORT flash",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth", "usb"],
        notes=["Confirm USB/JTAG and strapping-pin assignments for the exact S3 module."]
    ),
    FirmwareTarget(
        id="stm32f746-nucleo-hal", name="STM32F746 Nucleo", kind=TargetKind.mcu,
        vendor="STMicroelectronics", board="NUCLEO-F746ZG", mcu="STM32F746ZG", framework="STM32 HAL",
        language="c", build_system="CMake + arm-none-eabi-gcc", build_command="cmake --build build", flash_command="st-flash write build/firmware.bin 0x8000000",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "timers", "dma", "usb"],
        notes=["Generated project requires the matching STM32Cube HAL/device package."]
    ),
    FirmwareTarget(
        id="pixhawk-px4-fmu-v5", name="Pixhawk FMUv5", kind=TargetKind.flight_controller,
        vendor="Pixhawk", board="Pixhawk FMUv5", mcu="STM32F765", framework="PX4",
        language="cpp", build_system="PX4 + CMake", build_command="make px4_fmu-v5_default", flash_command="make px4_fmu-v5_default upload",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "barometer", "gps"],
        notes=["Integrate as a PX4 module or driver and test in SITL before hardware."]
    ),
    FirmwareTarget(
        id="arduino-nano-avr", name="Arduino Nano", kind=TargetKind.mcu,
        vendor="Arduino", board="Arduino Nano", mcu="ATmega328P", framework="Arduino AVR",
        language="cpp", build_system="PlatformIO", build_command="pio run", flash_command="pio run -t upload",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm"],
        notes=["Confirm classic Nano bootloader variant before flashing."]
    ),
    FirmwareTarget(
        id="stm32f103-bluepill-hal", name="STM32F103 Blue Pill", kind=TargetKind.mcu,
        vendor="STMicroelectronics", board="Blue Pill F103C8", mcu="STM32F103C8T6", framework="STM32 HAL",
        language="c", build_system="CMake + arm-none-eabi-gcc", build_command="cmake --build build", flash_command="st-flash write build/firmware.bin 0x8000000",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "timers", "dma"],
        notes=["Confirm clone board oscillator and flash size before flashing."]
    ),
    FirmwareTarget(
        id="speedybee-f405-betaflight", name="SpeedyBee F405 flight controller", kind=TargetKind.flight_controller,
        vendor="SpeedyBee", board="F405 flight controller", mcu="STM32F405", framework="Betaflight",
        language="c", build_system="Make", build_command="make TARGET=SPEEDYBEEF405", flash_command="dfu-util -D obj/firmware.bin",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "pwm", "imu", "osd"],
        notes=["Use the exact SpeedyBee target from the Betaflight target list; this profile is a reference integration point."]
    ),
    FirmwareTarget(
        id="matek-f722-inav", name="Matek F722 flight controller", kind=TargetKind.flight_controller,
        vendor="Matek", board="Matek F722", mcu="STM32F722", framework="INAV",
        language="c", build_system="Make", build_command="make TARGET=MATEKF722", flash_command="dfu-util -D obj/firmware.bin",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "gps", "osd"],
        notes=["Use the exact Matek target definition and verify board-specific peripheral mappings."]
    ),
    FirmwareTarget(
        id="betaflight-h7-generic", name="Betaflight STM32 H7 target", kind=TargetKind.flight_controller,
        vendor="Open flight-controller ecosystem", board="Generic STM32 H7 FC", mcu="STM32H743", framework="Betaflight",
        language="c", build_system="Make", build_command="make TARGET=GENERIC_H743", flash_command="dfu-util -D obj/firmware.bin",
        supported_peripherals=["gpio", "uart", "i2c", "spi", "can", "pwm", "imu", "osd"],
        notes=["Select the exact Betaflight target definition and verify target-specific pin mappings."]
    ),
])

TARGET_BY_ID = {target.id: target for target in TARGETS}


def list_targets() -> list[FirmwareTarget]:
    return TARGETS.copy()


def find_target(target_id: str | None, description: str) -> FirmwareTarget | None:
    if target_id:
        return TARGET_BY_ID.get(target_id)
    text = description.lower()
    aliases = {
        "esp32-devkitc-esp32-idf": ["esp32", "esp32 devkit", "esp32-wroom", "esp-idf"],
        "esp32-s3-devkitc-esp-idf": ["esp32-s3", "esp32 s3"],
        "arduino-uno-avr": ["arduino uno", "atmega328", "avr"],
        "arduino-nano-avr": ["arduino nano"],
        "stm32f411-blackpill-hal": ["black pill", "stm32f411"],
        "stm32f746-nucleo-hal": ["nucleo-f746", "stm32f746"],
        "stm32f103-bluepill-hal": ["blue pill", "stm32f103"],
        "rp2040-pico-sdk": ["rp2040", "raspberry pi pico", "pico sdk"],
        "nrf52840-dk-zephyr": ["nrf52840", "zephyr", "nordic"],
        "pixhawk-px4-fmu-v6c": ["fmu-v6", "fmu v6"],
        "pixhawk-px4-fmu-v5": ["px4", "pixhawk", "fmu-v5", "fmu v5"],
        "pixhawk-ardupilot-cubeorange": ["ardupilot", "cube orange", "copter"],
        "betaflight-f4-generic": ["betaflight", "f4 flight controller"],
        "betaflight-h7-generic": ["betaflight h7", "h7 flight controller"],
        "speedybee-f405-betaflight": ["speedybee f405", "speedybee"],
        "inav-f7-generic": ["inav", "f7 flight controller"],
        "matek-f722-inav": ["matek f722", "matek"],
    }
    scores = {target_id: sum(len(alias) for alias in terms if alias in text) for target_id, terms in aliases.items()}
    best_id = max(scores, key=scores.get) if scores else None
    return TARGET_BY_ID.get(best_id) if best_id and scores[best_id] > 0 else None
