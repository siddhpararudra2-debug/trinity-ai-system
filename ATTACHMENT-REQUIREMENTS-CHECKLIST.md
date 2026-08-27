# Attachment-driven firmware implementation checklist

## Backend firmware capability

The requested system needs a universal firmware engine with a parent strategy and specialized submodules for bare-metal C/C++, RTOS, FPGA HDL, MicroPython/CircuitPython, embedded Rust, ARM/RISC-V assembly, bootloader/security, Linux device drivers, and build systems. Supported project conventions include CMSIS/vendor HAL, Arduino setup/loop, ESP-IDF components and Kconfig, Zephyr device-tree overlays, FreeRTOS tasks and ISR-safe APIs, embedded-hal/no_std Rust, synthesizable Verilog/VHDL with testbenches, and CMake/Make/PlatformIO/linker/device-tree files.

The input contract should accept the query plus optional target hardware, framework, and language. The output should include code or multi-file project data, detected language, target, confidence, dependencies, filename, rendering hints, resource estimates, validation warnings, and security findings.

## Routing, context, and LLM integration

The orchestrator must detect firmware keywords with higher priority than math or generic hardware when firmware context is present, extract common hardware identifiers, support compound multi-engine workflows, and pass upstream results into downstream firmware generation. The firmware pipeline should support paradigm-specific prompt templates, context buffers for iterative refinement, token-aware multi-call generation, and environment-configured asynchronous LLM integration. Credentials and model settings must use TRINITY-prefixed environment variables.

## Validation and security

Generated firmware must be parsed from structured output, syntax-checked where toolchains are available, statically analyzed, scanned for dangerous patterns, and checked against a dependency allowlist. Security-critical findings such as hardcoded keys, backdoors, unsafe copies, disabled safety mechanisms, unbounded allocations, reserved-address writes, or permanently disabled interrupts should block or prominently warn. A hardware knowledge base should cover MCU specifications, peripherals, register mappings, pin functions, and conflicts.

## Frontend

Firmware responses need a terminal-consistent syntax-highlighted code viewer supporting C/C++, Rust, Python/MicroPython, Verilog, VHDL, assembly, and build files. It should show language and filename, line numbers, horizontal and vertical scrolling, expand/collapse, copy, single-file download, multi-file tabs, ZIP download, dependency verification, security warnings, validation findings, and resource dashboard progress indicators.

## Multi-file projects and iteration

The engine should detect multi-file requests, preserve directory structure, generate a README, package projects into ZIP archives, retain each generated version in conversation history, support modification requests against prior code, and optionally show diffs and restore earlier versions.

## QA and operations

The implementation should include platform detection tests, representative generation and validation tests, prompt-injection/security penetration coverage, resource-estimation tests, caching for standard templates, per-user rate limiting and token budgets, usage visibility, feedback capture, and a documented maintenance process for hardware/toolchain updates.
