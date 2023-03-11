#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <intrin.h>

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

EFI_STATUS EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	SystemTable->ConOut->OutputString(SystemTable->ConOut, L"baton drop into efi payload\n");
	InfLoop();
}