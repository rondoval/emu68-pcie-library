# emu68-pcie-library Agent Notes

## Role

- This repo currently ships the PCIe support code as a static library linked into consumers, not as a shared opened-by-name Amiga library.
- The intended direction is to turn `pcie.library` into a real dynamic library shared between multiple users; treat upcoming work in this repo as part of that transition unless a task is explicitly limited to current behavior.
- `lspci` is the standalone bring-up and debugging tool for enumeration and BAR assignment.

## Build

- Required installed dependencies: `emu68-common` and `emu68-gic400-library`.
- Preferred commands:
  - `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_PREFIX_PATH=/path/to/emu68-driver-stack -DCMAKE_INSTALL_PREFIX=/path/to/emu68-driver-stack`
  - `cmake --build build`
  - `cmake --install build`

## Code Handling

- Be conservative with public PCIe API changes; `emu68-xhci-driver` links this directly.
- Keep the future shared-library design in mind when introducing new state, ownership assumptions, or APIs, even if the current implementation is still static-linked.
- Preserve the current BCM2711 + VL805 assumptions unless the task is explicitly widening hardware support.
- Changes to enumeration, BAR assignment, or MSI setup should be validated with both library consumers and `lspci` where possible.
- Do not treat `pcie.library` as a real shared library yet when reasoning about current runtime state ownership, but avoid cementing assumptions that would block the planned multi-user dynamic-library model.

## Validation

- Small changes can be validated in this repo alone.
- Interface or behavior changes that affect xHCI should also be validated by rebuilding `emu68-xhci-driver`, preferably through `emu68-driver-stack`.

