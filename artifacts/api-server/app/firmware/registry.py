"""Explicit firmware target registry.

A target is intentionally concrete: code generation is allowed only for a
profile in this registry, so unsupported board/MCU combinations are rejected.
"""
from __future__ import annotations

from app.firmware.knowledge import extract_hardware_identifier, lookup_hardware
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

for _target in TARGETS:
    _hardware = lookup_hardware(_target.mcu)
    if _hardware:
        _target.architecture = _target.architecture or _hardware.get("architecture")
        _target.flash_bytes = _target.flash_bytes or _hardware.get("flash_bytes")
        _target.ram_bytes = _target.ram_bytes or _hardware.get("ram_bytes")
        _target.clock_hz = _target.clock_hz or _hardware.get("clock_hz")
        _target.supported_peripherals = sorted(set(_target.supported_peripherals) | set(_hardware.get("peripherals", [])))

TARGET_BY_ID = {target.id: target for target in TARGETS}


def list_targets() -> list[FirmwareTarget]:
    return TARGETS.copy()


def find_target(target_id: str | None, description: str, framework: str | None = None, language: str | None = None) -> FirmwareTarget | None:
    if target_id:
        return TARGET_BY_ID.get(target_id)
    text = f"{description} {framework or ''} {language or ''}".lower()
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
    explicit_paradigm = any(token in text for token in ("micropython", "circuitpython", "verilog", "systemverilog", "vhdl", "fpga", "embedded rust", "embedded-hal", "no_std", "linux driver", "kernel module", "assembly", "risc-v", "riscv"))
    scores = {} if explicit_paradigm else {target_id: sum(len(alias) for alias in terms if alias in text) for target_id, terms in aliases.items()}
    best_id = max(scores, key=scores.get) if scores else None
    if best_id and scores[best_id] > 0:
        return TARGET_BY_ID.get(best_id)

    hardware_id = extract_hardware_identifier(description)
    knowledge = lookup_hardware(hardware_id)
    if any(item in text for item in ("verilog", "systemverilog", "vhdl", "fpga")):
        selected_language = "vhdl" if "vhdl" in text else "verilog"
        return FirmwareTarget(id="generic-fpga", name="Generic FPGA", kind=TargetKind.fpga, vendor="Unspecified", board="Generic FPGA", mcu="FPGA", framework=framework or "FPGA HDL", language=selected_language, build_system="Icarus Verilog/GHDL", build_command="iverilog -g2012 -o build/design.out *.v", flash_command="vendor-specific FPGA programmer", supported_peripherals=["gpio", "clock", "reset"], notes=["Verify the FPGA family, constraints, clocking, and synthesis tool before deployment."])
    if "arduino" in text:
        return FirmwareTarget(id="generic-arduino", name="Generic Arduino-compatible board", kind=TargetKind.generic, vendor="Arduino ecosystem", board="Arduino-compatible board", mcu=hardware_id or "unspecified", framework=framework or "Arduino", language=language or "cpp", build_system="PlatformIO", build_command="pio run", flash_command="pio run -t upload", supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm"], notes=["Confirm the board definition, bootloader, pins, and library versions before upload."])
    if "freertos" in text or "free rtos" in text:
        return FirmwareTarget(id="generic-freertos", name="Generic FreeRTOS target", kind=TargetKind.mcu, vendor="FreeRTOS ecosystem", board="FreeRTOS board", mcu=hardware_id or "unspecified", framework="FreeRTOS", language=language or "c", build_system="CMake", build_command="cmake --build build", flash_command="vendor-specific programmer", supported_peripherals=["gpio", "uart", "i2c", "spi", "timers", "dma"], notes=["Set the exact port, heap implementation, tick rate, and interrupt priority rules before deployment."])
    if "zephyr" in text and not any(alias in text for alias in ("nrf52840", "nrf52840 dk")):
        return FirmwareTarget(id="generic-zephyr", name="Generic Zephyr board", kind=TargetKind.mcu, vendor="Zephyr ecosystem", board="Zephyr board", mcu=hardware_id or "unspecified", framework="Zephyr", language=language or "c", build_system="west + CMake", build_command="west build", flash_command="west flash", supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm"], notes=["Confirm the Zephyr board target and device-tree bindings before flashing."])
    if "esp-idf" in text and "esp32" not in text:
        return FirmwareTarget(id="generic-esp-idf", name="Generic ESP-IDF target", kind=TargetKind.mcu, vendor="Espressif", board="ESP32-family board", mcu=hardware_id or "ESP32-family", framework="ESP-IDF", language=language or "c", build_system="CMake + idf.py", build_command="idf.py build", flash_command="idf.py -p PORT flash", supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm", "wifi", "bluetooth"], notes=["Confirm the exact SoC, module, strapping pins, and SDK version."])
    if any(item in text for item in ("micropython", "circuitpython")):
        return FirmwareTarget(id="generic-micropython", name="Generic MicroPython board", kind=TargetKind.generic, vendor="Unspecified", board="MicroPython board", mcu=hardware_id or "unspecified", framework=framework or "MicroPython", language="python", build_system="mpremote", build_command="mpremote run main.py", flash_command="mpremote fs cp main.py :main.py", supported_peripherals=["gpio", "uart", "i2c", "spi", "adc", "pwm"], notes=["Confirm the board port and module APIs before deployment."])
    if "rust" in text or "embedded-hal" in text or "no_std" in text:
        return FirmwareTarget(id="generic-embedded-rust", name="Generic embedded Rust target", kind=TargetKind.mcu, vendor="Unspecified", board="Embedded Rust board", mcu=hardware_id or "unspecified", framework=framework or "Embedded Rust", language="rust", build_system="Cargo", build_command="cargo check", flash_command="probe-rs run --chip CHIP", supported_peripherals=["gpio", "uart", "i2c", "spi", "timers"], notes=["Pin the exact HAL crate and target chip before flashing."])
    if "linux driver" in text or "kernel module" in text:
        return FirmwareTarget(id="generic-linux-driver", name="Generic embedded Linux board", kind=TargetKind.linux_board, vendor="Unspecified", board="Embedded Linux board", mcu=hardware_id or "linux", framework=framework or "Linux Kernel", language="c", build_system="Kbuild", build_command="make -C /lib/modules/$(uname -r)/build M=$PWD modules", flash_command="deploy through the target Linux image", supported_peripherals=["gpio", "uart", "i2c", "spi", "usb", "can"], notes=["Build against the exact target kernel headers and device tree."])
    if "assembly" in text or "risc-v" in text or "riscv" in text:
        return FirmwareTarget(id="generic-assembly", name="Generic embedded assembly target", kind=TargetKind.mcu, vendor="Unspecified", board="Embedded board", mcu=hardware_id or "unspecified", framework=framework or "Bare Metal Assembly", language="assembly", build_system="GNU binutils", build_command="make", flash_command="vendor-specific programmer", supported_peripherals=["gpio", "uart", "interrupt"], notes=["Verify ABI, ISA extension set, startup code, and linker script."])
    if knowledge:
        return FirmwareTarget(id=f"mcu-{knowledge['id'].lower()}", name=f"Knowledge-base target {knowledge['id']}", kind=TargetKind.mcu, vendor="Knowledge base", board=knowledge["id"], mcu=knowledge["id"], framework=framework or "Bare Metal", language=language or "c", build_system="CMake + vendor toolchain", build_command="cmake --build build", flash_command="vendor-specific programmer", architecture=knowledge.get("architecture"), flash_bytes=knowledge.get("flash_bytes"), ram_bytes=knowledge.get("ram_bytes"), clock_hz=knowledge.get("clock_hz"), supported_peripherals=knowledge.get("peripherals", []), notes=["Knowledge-base profile; confirm exact package, registers, pin map, and errata against the datasheet."])
    return None
