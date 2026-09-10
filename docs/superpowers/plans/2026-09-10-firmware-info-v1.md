# FirmwareInfoV1 Cross-Project Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fixed firmware-information header, device/firmware query commands, pre-erase compatibility enforcement, and version-aware Qt upgrade workflow across FOCTEST, Bootloader, and Qt.

**Architecture:** The APP pre-build generator emits one byte-stable `FirmwareInfoV1` initializer linked at `0x08020000`; the vector table moves to `0x08020200`. APP and Bootloader expose identical APP1 query payloads. Qt parses the same 256-byte format from HEX, checks it against `DeviceInfoV1`, and sends the header inside `BL_BEGIN V2` so Bootloader rejects incompatible images before erasing Flash.

**Tech Stack:** ARMCC5/Keil uVision, STM32F407 HAL/CMSIS, C99 host tests with GCC, Python 3 generator tests, Qt 5.14.2/C++17/QTest, APP1 over WinUSB.

---

## Repository Safety

- Work directly in the existing repositories because the user explicitly declined isolation.
- Preserve `FOCTEST/MDK-ARM/FOCTEST.uvoptx`; it is a pre-existing user modification.
- Preserve `bootloaderStepbyStep/tools/usb_bos_probe/usb_bos_probe.exe`; it is a pre-existing untracked file.
- Do not modify generated Keil `Objects`, `Listings`, or deployed binaries except through verified builds.

### Task 1: APP firmware-information format and generator

**Files:**
- Create: `FOCTEST/config/firmware_profile.json`
- Create: `FOCTEST/USER/firmware_info.h`
- Create: `FOCTEST/USER/firmware_info.c`
- Create: `FOCTEST/tools/generate_firmware_info.py`
- Create: `FOCTEST/tests/test_generate_firmware_info.py`
- Create: `FOCTEST/tests/firmware_info_test.c`
- Modify: `FOCTEST/.gitignore`

- [ ] **Step 1: Write failing generator tests**

Test the exact 256-byte layout, `FWI1`, offsets, zero padding, CRC check value `0xCBF43926`, clean Debug flags, dirty Debug flags, and Release rejection of a dirty temporary Git repository. The golden assertion must include:

```python
assert len(blob) == 256
assert blob[0:4] == b"FWI1"
assert struct.unpack_from("<I", blob, 0x08)[0] == 0x00010001
assert struct.unpack_from("<I", blob, 0x1C)[0] == 0x08020000
assert struct.unpack_from("<I", blob, 0x20)[0] == 0x08020200
assert crc32_iso_hdlc(blob[:0xFC]) == struct.unpack_from("<I", blob, 0xFC)[0]
```

- [ ] **Step 2: Run RED**

Run: `python -m unittest tests.test_generate_firmware_info -v`

Expected: FAIL because `tools.generate_firmware_info` does not exist.

- [ ] **Step 3: Implement the generator and format API**

Define natural-aligned fixed-width fields and compile-time size/offset checks in `firmware_info.h`. `firmware_info.c` exposes `FirmwareInfo_Get()` and includes ignored generated initializer data. The Python generator strictly validates the approved JSON profile, queries Git, applies flags, calculates CRC-32/ISO-HDLC, and writes a deterministic C initializer include.

- [ ] **Step 4: Add C golden-vector tests and run GREEN**

Compile and run:

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror tests\firmware_info_test.c USER\firmware_info.c -IUSER -o $env:TEMP\firmware_info_test.exe
& $env:TEMP\firmware_info_test.exe
python -m unittest tests.test_generate_firmware_info -v
```

Expected: all tests pass with no warnings.

- [ ] **Step 5: Commit APP generator foundation**

Commit only Task 1 files with message `feat: add generated firmware information header`.

### Task 2: APP fixed Flash layout and build verification

**Files:**
- Create: `FOCTEST/MDK-ARM/FOCTEST.sct`
- Create: `FOCTEST/tools/verify_firmware_layout.py`
- Create: `FOCTEST/tests/test_verify_firmware_layout.py`
- Modify: `FOCTEST/Core/Src/system_stm32f4xx.c`
- Modify: `FOCTEST/MDK-ARM/FOCTEST.uvprojx`

- [ ] **Step 1: Write failing layout-verifier tests**

Build synthetic Intel HEX fixtures and assert rejection when the header is absent, vectors remain at offset zero, ordinary code starts below `0x08020400`, or any byte reaches `0x080E0000`.

- [ ] **Step 2: Run RED**

Run: `python -m unittest tests.test_verify_firmware_layout -v`

Expected: FAIL because the verifier does not exist.

- [ ] **Step 3: Add Scatter and Keil integration**

Use these execution bases:

```text
ER_FW_INFO 0x08020000 size 0x100 -> firmware_info object `.firmware_info`
ER_VECTORS 0x08020200 size 0x200 -> startup object `RESET`
ER_APP_CODE 0x08020400 size 0xBFC00 -> remaining RO/load content
RW_IRAM1 0x20000000 size 0x1C000 -> existing RW/ZI content
```

Change `VECT_TAB_OFFSET` to `0x00020200U`, set IROM start/size to `0x08020000/0x000C0000`, select the Scatter file, add `firmware_info.c`, and configure Debug pre-build generation plus post-build layout verification. Add a clean-Release target only by cloning the project target while leaving `.uvoptx` untouched.

- [ ] **Step 4: Run GREEN and Keil build**

Run Python tests, then:

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -b 'I:\MYFOC\YuanziHardwareWithMyFirmware\FOCTEST\MDK-ARM\FOCTEST.uvprojx' -t 'FOCTEST_Debug' -o 'I:\MYFOC\YuanziHardwareWithMyFirmware\FOCTEST\MDK-ARM\firmware_info_build.log'
```

Run the layout verifier against the generated HEX and inspect the Map for `0x08020000`, `0x08020200`, and `0x08020400`.

- [ ] **Step 5: Commit APP layout**

Commit with message `feat: place firmware header before application vectors`.

### Task 3: APP device and firmware query commands

**Files:**
- Create: `FOCTEST/USER/device_info.h`
- Create: `FOCTEST/USER/device_info.c`
- Create: `FOCTEST/USER/firmware_info_text.h`
- Create: `FOCTEST/USER/firmware_info_text.c`
- Create: `FOCTEST/tests/device_info_test.c`
- Create: `FOCTEST/tests/firmware_info_text_test.c`
- Modify: `FOCTEST/USER/bl_upgrade_protocol.h`
- Modify: `FOCTEST/USER/app_upgrade_entry.h`
- Modify: `FOCTEST/USER/app_upgrade_entry.c`
- Modify: `FOCTEST/USER/freq_test.c`
- Modify: `FOCTEST/USB_DEVICE/App/usbd_upgrade_if.c`
- Modify: `FOCTEST/MDK-ARM/FOCTEST.uvprojx`
- Modify: `FOCTEST/tests/app_upgrade_entry_test.c`
- Modify: `FOCTEST/tests/winusb_app_contract_test.ps1`

- [ ] **Step 1: Write failing APP1 and text-command tests**

Assert empty requests for `0x0003` and `0x0004`, exact 28-byte `DeviceInfoV1`, exact 256-byte firmware response, new capability bits `0x18`, malformed-request errors, and one-line CDC output for `GET_DEVICE_INFO` and `GET_FIRMWARE_INFO`.

- [ ] **Step 2: Run RED**

Compile `app_upgrade_entry_test.c` with the existing protocol sources and compile the two new host tests. Expected: compilation fails on missing commands/types.

- [ ] **Step 3: Implement query data sources and handlers**

Add protocol constants:

```c
#define BL_UPGRADE_TYPE_GET_DEVICE_INFO   UINT16_C(0x0003)
#define BL_UPGRADE_TYPE_GET_FIRMWARE_INFO UINT16_C(0x0004)
#define BL_UPGRADE_CAPABILITY_DEVICE_INFO UINT32_C(0x00000008)
#define BL_UPGRADE_CAPABILITY_FIRMWARE_INFO UINT32_C(0x00000010)
```

Encode UID words at `UID_BASE + 0/+4/+8` little-endian. APP1 reads information through injected ports for host testing. CDC delegates the two exact command lines before frequency-test parsing and formats from the same structures without blocking or allocating memory.

- [ ] **Step 4: Run GREEN, contracts, and Keil build**

Run new host executables, `tests/winusb_app_contract_test.ps1`, existing reboot contracts, and a full `FOCTEST_Debug` build.

- [ ] **Step 5: Commit APP queries**

Commit with message `feat: expose application device and firmware information`.

### Task 4: Bootloader firmware validation and vector relocation

**Files:**
- Create: `bootloaderStepbyStep/Bootloader/Inc/bl_firmware_info.h`
- Create: `bootloaderStepbyStep/Bootloader/Src/bl_firmware_info.c`
- Create: `bootloaderStepbyStep/Bootloader/Inc/bl_device_identity.h`
- Create: `bootloaderStepbyStep/Bootloader/Src/bl_device_identity.c`
- Create: `bootloaderStepbyStep/tests/host/test_bl_firmware_info.c`
- Modify: `bootloaderStepbyStep/Bootloader/Inc/bl_config.h`
- Modify: `bootloaderStepbyStep/Bootloader/Src/bl_jump.c`
- Modify: `bootloaderStepbyStep/Bootloader/Src/bl_board.c`
- Modify: `bootloaderStepbyStep/tests/host/test_bl_jump.c`
- Modify: `bootloaderStepbyStep/tests/host/Makefile`
- Modify: `bootloaderStepbyStep/MDK-ARM/bootloaderStepbyStep.uvprojx`

- [ ] **Step 1: Write failing validation and relocated-vector tests**

Cover magic/schema/size/flags/reserved/CRC/product/hardware/address failures, MSP boundaries/alignment, Reset_Handler below `0x08020400`, and VTOR assignment to `0x08020200`.

- [ ] **Step 2: Run RED**

Run: `mingw32-make -C tests\host clean test`

Expected: compilation fails because `bl_firmware_info` and `BL_APPLICATION_VECTOR_BASE` are absent.

- [ ] **Step 3: Implement validation and boot integration**

Define `BL_APPLICATION_VECTOR_BASE 0x08020200`, validate the exact V1 bytes without unaligned struct dereferences, update board vector reads and `SCB->VTOR`, and preserve existing rule that RECEIVING/ABORTED state blocks boot. Absence of a state record remains compatible with direct Keil debug only when the new header and vectors validate.

- [ ] **Step 4: Run GREEN and Keil build**

Run all host tests and build `bootloaderStepbyStep.uvprojx` with ARMCC5.

- [ ] **Step 5: Commit Bootloader validation**

Commit with message `feat: validate firmware header before application jump`.

### Task 5: Bootloader query commands and BL_BEGIN V2

**Files:**
- Modify: `bootloaderStepbyStep/Bootloader/Inc/bl_upgrade_protocol.h`
- Modify: `bootloaderStepbyStep/Bootloader/Inc/bl_upgrade_service.h`
- Modify: `bootloaderStepbyStep/Bootloader/Src/bl_upgrade_service.c`
- Modify: `bootloaderStepbyStep/Bootloader/Src/bl_board.c`
- Modify: `bootloaderStepbyStep/USB_DEVICE/App/usbd_vendor_if.c`
- Modify: `bootloaderStepbyStep/tests/host/test_bl_upgrade_protocol.c`
- Modify: `bootloaderStepbyStep/tests/host/test_bl_upgrade_service.c`
- Modify: `bootloaderStepbyStep/tests/winusb_bootloader_contract_test.ps1`

- [ ] **Step 1: Write failing protocol/service tests**

Assert 320-byte payload support, both query responses, exact 272-byte BEGIN, no erase on each compatibility failure, header programming before data, returned resume offset 256, data rejection below offset 256, interrupted-resume behavior, and whole-image CRC verification.

- [ ] **Step 2: Run RED**

Run all Bootloader host tests. Expected: new BEGIN and query tests fail against V1 behavior.

- [ ] **Step 3: Implement V2 transaction**

Decode BEGIN fields at offsets `0/2/4/8/12/16`, validate before saving or erasing, save RECEIVING at offset zero, erase, write the header, then persist offset 256. On resume at zero repeat erase/header programming; on resume at or above 256 require the Flash header to match the supplied header. Keep BL_DATA payload maximum 246 and service data maximum 240 as currently advertised.

- [ ] **Step 4: Run GREEN, contracts, and Keil build**

Run `mingw32-make -C tests\host clean test`, PowerShell contract tests, and an ARMCC5 full build.

- [ ] **Step 5: Commit Bootloader protocol**

Commit with message `feat: validate firmware identity in begin v2`.

### Task 6: Qt firmware and device codecs

**Files:**
- Create: `bootloaderQtStepbyStep/src/FirmwareInfo.h`
- Create: `bootloaderQtStepbyStep/src/FirmwareInfo.cpp`
- Create: `bootloaderQtStepbyStep/src/DeviceInfo.h`
- Create: `bootloaderQtStepbyStep/src/DeviceInfo.cpp`
- Modify: `bootloaderQtStepbyStep/src/IntelHexParser.h`
- Modify: `bootloaderQtStepbyStep/src/IntelHexParser.cpp`
- Modify: `bootloaderQtStepbyStep/src/App1Codec.h`
- Modify: `bootloaderQtStepbyStep/bootloaderQtStepbyStep.pro`
- Modify: `bootloaderQtStepbyStep/tests/tests.pro`
- Modify: `bootloaderQtStepbyStep/tests/tst_firmwarecore.cpp`

- [ ] **Step 1: Write failing QTests**

Add golden header/device payload cases and failures for bad CRC, flags, UTF-8, missing NUL, nonzero reserved bytes, wrong addresses, old vector-at-base HEX, and vector validation at offset `0x200`.

- [ ] **Step 2: Run RED**

Build and run `firmware_core_tests.exe` offscreen. Expected: compilation fails because the codecs are absent.

- [ ] **Step 3: Implement codecs and parser integration**

`FirmwareInfo::decode()` parses fixed offsets and exposes semantic version comparison/display. `DeviceInfo::decode()` parses 28 bytes. `IntelHexParser` requires all 256 header bytes to be explicitly present in the HEX, validates vectors at image offset `0x200`, and stores parsed metadata in `FirmwareImage`. Increase APP1 max payload to 320 and add the two command constants/capabilities.

- [ ] **Step 4: Run GREEN**

Rebuild and run the full Qt test executable plus `winusb_overlapped_contract_test.ps1`.

- [ ] **Step 5: Commit Qt codecs**

Commit with message `feat: parse firmware and device information`.

### Task 7: Qt compatibility UI and V2 upgrade state machine

**Files:**
- Modify: `bootloaderQtStepbyStep/src/FirmwareUpgradeController.h`
- Modify: `bootloaderQtStepbyStep/src/FirmwareUpgradeController.cpp`
- Modify: `bootloaderQtStepbyStep/src/FirmwareUpgradeDialog.h`
- Modify: `bootloaderQtStepbyStep/src/FirmwareUpgradeDialog.cpp`
- Modify: `bootloaderQtStepbyStep/tests/tst_firmwarecore.cpp`

- [ ] **Step 1: Write failing controller and UI tests**

Test query order `GET_MODE -> GET_DEVICE_INFO -> GET_FIRMWARE_INFO`, product/hardware rejection before ENTER/BEGIN, higher/same/lower SemVer policy, downgrade checkbox override, 272-byte BEGIN encoding, initial acknowledged offset 256, and post-reboot version/hash confirmation.

- [ ] **Step 2: Run RED**

Run the QTest executable. Expected: the new state-machine and widget assertions fail.

- [ ] **Step 3: Implement controller and UI**

Add explicit query stages, store `DeviceInfo` and installed `FirmwareInfo`, emit device-detail/compatibility signals, and require compatibility before mode transition. BEGIN encodes schema 2, size, base, CRC and header; transfer starts at offset 256. Add a readable metadata panel, compatibility status, and `允许固件降级` checkbox shown/enabled only for a lower SemVer. Re-query and compare SemVer/build/hash after reboot before reporting completion.

- [ ] **Step 4: Run GREEN and build release executable**

Run all Qt tests offscreen, build `MYFOCFirmwareUpdater.exe`, and update `deploy` only after the executable starts successfully.

- [ ] **Step 5: Commit Qt workflow**

Commit with message `feat: enforce firmware compatibility during upgrade`.

### Task 8: Documentation, cross-project regression, and hardware verification

**Files:**
- Modify: `bootloaderQtStepbyStep/README.md`
- Modify: `bootloaderStepbyStep/README.md`
- Modify: `FOCTEST/README.md` if present, otherwise create `FOCTEST/docs/firmware_info.md`
- Modify: `I:/MYFOC/protocal/Ident/README.md`

- [ ] **Step 1: Update protocol and operator documentation**

Document the fixed layout, both query payloads, BEGIN V2 bytes, downgrade behavior, Debug/Release generation, Keil debugging limitation, and all stable error details.

- [ ] **Step 2: Run fresh complete verification**

Run every new and existing host test, all PowerShell contract tests, both Keil builds, Qt tests, Qt release build, `git diff --check`, layout verification, and secret/artifact scans. Record exact pass counts and binary/map addresses.

- [ ] **Step 3: Flash and exercise powered hardware**

Use CMSIS-DAP through the existing Keil target settings. Build before flash; flash Bootloader first, then install the new APP through the Qt V2 path. Verify USB discovery in both modes, GET_DEVICE_INFO, GET_FIRMWARE_INFO, compatibility rejection with a test-mutated in-memory fixture, complete upgrade, reset jump, and post-upgrade version/hash readback. Do not flash intentionally malformed images.

- [ ] **Step 4: Final review and commits**

Review the three repository diffs against the design, fix all Critical/Important findings, rerun affected tests, commit documentation in the owning repositories, and leave the user-owned `.uvoptx` and probe executable unchanged.
