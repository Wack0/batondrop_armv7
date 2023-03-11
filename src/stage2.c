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

// crt divide code uses this
__declspec(noreturn) void __helper_divide_by_0() {
	InfLoop();
}

typedef EFI_STATUS (*tfpEfiMain)(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable);

typedef void (*tfpBlpArchSwitchContext)(DWORD type);

typedef struct _STAGE2_ENTRY_BLOCK {
	EFI_HANDLE ImageHandle;
	EFI_SYSTEM_TABLE* SystemTable;
	tfpBlpArchSwitchContext BlpArchSwitchContext;
	PVOID Stage2Base;
	PVOID PayloadBase;
} STAGE2_ENTRY_BLOCK;

__declspec(noreturn) void Stage2Entry(STAGE2_ENTRY_BLOCK Context) {
	// Switch to firmware context, to use EFI system services.
	Context.BlpArchSwitchContext(1);
	
	// Get the image handle and system table from the entry block.
	EFI_HANDLE ImageHandle = Context.ImageHandle;
	EFI_SYSTEM_TABLE* SystemTable = Context.SystemTable;
	
	// Get the boot services from the system table.
	EFI_BOOT_SERVICES* BS = SystemTable->BootServices;

	// Get the memory map.
	UINTN MemoryMapSize = 0;
	EFI_MEMORY_DESCRIPTOR* Buffer = NULL;
	UINTN MapKey = 0;
	UINTN DescriptorSize = 0;
	UINT32 DescriptorVersion = 0;
	EFI_STATUS Status = BS->GetMemoryMap(&MemoryMapSize, Buffer, &MapKey, &DescriptorSize, &DescriptorVersion);
	while (Status == EFI_BUFFER_TOO_SMALL) {
		// Add space for some extra entries if needed.
		MemoryMapSize += DescriptorSize * 0x10;
		if (Buffer != NULL) BS->FreePool(Buffer);
		Status = BS->AllocatePool(EfiLoaderData, MemoryMapSize, (VOID**)&Buffer);
		if (EFI_ERROR(Status)) {
			SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Could not allocate memory for EFI memory map\n");
			InfLoop();
		}
		Status = BS->GetMemoryMap(&MemoryMapSize, Buffer, &MapKey, &DescriptorSize, &DescriptorVersion);
	}
	
	// Free all memory allocated by the Windows boot environment, except for the stack, the currently running code, and the payload.
	UINTN CountDescriptors = MemoryMapSize / DescriptorSize;
	UINTN pDescriptor = (UINTN)Buffer;
	UINTN pStack = (UINTN)&MemoryMapSize;
	for (UINTN i = 0; i < CountDescriptors; i++, pDescriptor += DescriptorSize) {
		EFI_MEMORY_DESCRIPTOR* Descriptor = (EFI_MEMORY_DESCRIPTOR*)pDescriptor;
		// All memory allocated by the Windows boot environment is of type EfiLoaderCode.
		if (Descriptor->Type != EfiLoaderCode) continue;
		
		// If this is payload or currently running code, ignore it.
		if (Descriptor->PhysicalStart == (UINTN)Context.Stage2Base) continue;
		if (Descriptor->PhysicalStart == (UINTN)Context.PayloadBase) continue;
		
		// If this is the current stack, ignore it.
		UINT64 PhysicalEnd = ((Descriptor->NumberOfPages) << 12);
		PhysicalEnd += Descriptor->PhysicalStart;
		if (pStack >= (UINTN)Descriptor->PhysicalStart && pStack < PhysicalEnd) continue;
		
		// Free this memory.
		BS->FreePages(Descriptor->PhysicalStart, Descriptor->NumberOfPages);
	}
	
	// Free allocated memory descriptors.
	BS->FreePool(Buffer);
	
	// Call the entrypoint of the payload.
	PIMAGE_DOS_HEADER Mz = (PIMAGE_DOS_HEADER)Context.PayloadBase;
	PIMAGE_NT_HEADERS Pe = (PIMAGE_NT_HEADERS)((size_t)Mz + Mz->e_lfanew);
	tfpEfiMain EfiMain = (tfpEfiMain)((size_t)Mz + Pe->OptionalHeader.AddressOfEntryPoint);
	EfiMain(ImageHandle, SystemTable);

	InfLoop();
}