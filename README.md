# baton drop for armv7 (MSM8960)

Given that policyhax (aka golden key)'s fix actually works on Qualcomm systems, I picked up a working but sold for-parts Dell XPS 10 to port baton drop to MSM8960.

Here is the result.

Extract image.7z to your GPT fat32-formatted USB device, copy your unsigned EFI boot application to \boot.efi, boot your MSM8960 Windows RT device with it, enjoy.

All payload src (including `divide.obj` from MS CRT, which stage2 requires), is included. For building, use an MSVC cross-compiler command prompt. Run `make_cert.bat` to create a self-signed cert, `build_mcupdate.bat` to build stage1, `build_stage2.bat` to build stage2, `build_boot.bat` to build the hello world `boot.efi`.

## Exploitation specifics
- Physical address cut off is at `0xA000_0000`. It could probably be set later if you wanted to just use baton drop for a UMCI jailbreak (when exploiting baton drop, advanced options menu shows nointegritychecks option); remember that on win8.x exploitation requires `osdevice` to be a BitLocker encrypted volume with key flag bit 0 set (can be set manually as is the case for `payload.vhd`, but is usually set for VMK sealed by TPM with secure boot for integrity validation).
  - Please note that setting the cut off to `0xC000_0000` works for baton drop to load a payload, but in a dual boot situation would cause NT to fail to boot.
- `hal.dll` has been patched to add `mcupdate.dll` to its imports, and self-signed.
- `mcupdate.dll` loads `efiesp:\stage2.dll` and `efiesp:\boot.efi`, obtains all pointers that stage2 needs and calls stage2 entrypoint.
- stage2:
  - calls `BlpArchSwitchContext` to switch back to firmware context to call EFI services
  - walks through the EFI memory map to free everything allocated by the Windows bootloaders (except for what stage1 loaded and the stack which points to memory allocated by bootmgr)
  - calls boot.efi entrypoint
- Any error (or boot.efi returning) will lead to infinite loop.

