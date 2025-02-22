#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifdef TARGET_OS_IPHONE
#include <mach/mach.h>
#include <mach/memory_entry.h>
#include <string>


class MachJitWorkaround {
public:
	MachJitWorkaround();

	static void ReallocateAreaWithOwnership(vm_address_t address, int size);
	static vm_address_t AllocateSharedMemory(unsigned long size, bool reserve);
	static void DestroySharedMemory(vm_address_t handle, unsigned long size);
	static vm_address_t MapView(vm_address_t sharedMemory, unsigned long srcOffset, vm_address_t location, unsigned long size);
	static void UnmapView(vm_address_t location, unsigned long size);

private:
	static const int DEFAULT_CHUNK_SIZE = 1024 * 1024; 
	static const mach_port_t _selfTask;

	static int CalculateOptimalChunkSize(int totalSize);
	static vm_address_t ReallocateBlock(vm_address_t address, int size);
	static void HandleMachError(int error, std::string operation);
};


#endif
