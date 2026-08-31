# Firmware worker container

[`Dockerfile`](Dockerfile) and [`entrypoint.sh`](entrypoint.sh) define the isolated firmware worker image. The API’s deterministic generator and validator live in `artifacts/api-server/app/firmware/`; this folder is the execution boundary for future/opt-in target toolchains.

The current repository does not install every MCU, flight-controller, FPGA, or embedded-Rust toolchain in this image. A build is only attempted when `TRINITY_ENABLE_FIRMWARE_BUILDS=1` and the registered command/toolchain is present. A skipped build is not a passing compile result.

Before using this worker, add pinned toolchains, resource limits, non-root execution, source/output mounts, logs, and a test matrix for each supported target.
