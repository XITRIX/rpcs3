#include "mach_jit_workaround.hpp"

#ifdef TARGET_OS_IPHONE

const mach_port_t MachJitWorkaround::_selfTask = mach_task_self();

MachJitWorkaround::MachJitWorkaround() {}

void MachJitWorkaround::ReallocateAreaWithOwnership(vm_address_t address, int size) {
	int chunkSize = CalculateOptimalChunkSize(size);
	vm_address_t currentAddress = address;
	vm_address_t endAddress = address + size;

	while (currentAddress < endAddress)
	{
		int blockSize = std::min(chunkSize, static_cast<int>(endAddress - currentAddress));
		ReallocateBlock(currentAddress, blockSize);
		currentAddress += blockSize;
	}
}

vm_address_t MachJitWorkaround::AllocateSharedMemory(unsigned long size, bool reserve) {
	vm_address_t address = 0;
	HandleMachError(vm_allocate(_selfTask, &address, size, VM_FLAGS_ANYWHERE), "vm_allocate");
	return address;
}

void MachJitWorkaround::DestroySharedMemory(vm_address_t handle, unsigned long size) {
	if (handle != 0 && size > 0) {
		vm_deallocate(_selfTask, handle, size);
	}
}

vm_address_t MachJitWorkaround::MapView(vm_address_t sharedMemory, unsigned long srcOffset, vm_address_t location, unsigned long size) {
	if (size == 0 || sharedMemory == 0)
	{
//		throw new ArgumentException("Invalid mapping parameters");
	}

	vm_address_t srcAddress = sharedMemory + srcOffset;
	vm_address_t dstAddress = location;
	int curProtection = 0;
	int maxProtection = 0;

	HandleMachError(
		vm_remap(
			_selfTask,
			&dstAddress,
			size,
			0,
			VM_FLAGS_OVERWRITE,
			_selfTask,
			srcAddress,
			0,
			&curProtection,
			&maxProtection,
			VM_INHERIT_DEFAULT),
		"vm_remap");

	return dstAddress;
}

void MachJitWorkaround::UnmapView(vm_address_t location, unsigned long size) {
	if (location != 0 && size > 0) {
		vm_deallocate(_selfTask, location, size);
	}
}

int MachJitWorkaround::CalculateOptimalChunkSize(int totalSize) {
	// Dynamically calculate chunk size based on total allocation size
	// For smaller allocations, use smaller chunks to avoid waste
	if (totalSize <= DEFAULT_CHUNK_SIZE)
	{
		return totalSize;
	}

	int chunkCount = std::max(4, totalSize / DEFAULT_CHUNK_SIZE);
	return (totalSize + chunkCount - 1) / chunkCount;
}

vm_address_t MachJitWorkaround::ReallocateBlock(vm_address_t address, int size) {
	memory_object_size_t memorySize = size;
	mach_port_t memoryObjectPort = 0;

//	try
//	{
		// Create memory entry
		HandleMachError(
			mach_make_memory_entry_64(
				_selfTask,
				&memorySize,
				0,
				MAP_MEM_NAMED_CREATE | MAP_MEM_LEDGER_TAGGED |
				VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
				&memoryObjectPort,
				0),
			"make_memory_entry_64");

		if (memorySize != static_cast<unsigned long>(size))
		{
			printf("Memory allocation size mismatch. Requested: %d, Allocated: %llu\n", size, memorySize);
//			throw new InvalidOperationException($"Memory allocation size mismatch. Requested: {size}, Allocated: {(long)memorySize}");
		}

		// Set ownership
		HandleMachError(
			mach_memory_entry_ownership(
				memoryObjectPort,
				TASK_NULL,
				VM_LEDGER_TAG_DEFAULT,
				VM_LEDGER_FLAG_NO_FOOTPRINT),
			"memory_entry_ownership");

		vm_address_t mapAddress = address;

		// Map memory
		HandleMachError(
			vm_map(
				_selfTask,
				&mapAddress,
				memorySize,
				0,
				VM_FLAGS_OVERWRITE,
				memoryObjectPort,
				0,
				0,
				VM_PROT_READ | VM_PROT_WRITE,
				VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE,
				VM_INHERIT_COPY),
			"vm_map");

		if (address != mapAddress)
		{
			printf("Memory mapping address mismatch\n");
//			throw new InvalidOperationException("Memory mapping address mismatch");
		}

		return mapAddress;
//	}
//	catch(...) {}
//	finally
//	{
//		// Proper cleanup of memory object port
//		if (memoryObjectPort != IntPtr.Zero)
//		{
//			// mach_port_deallocate(_selfTask, memoryObjectPort);
//		}
//	}
}

void MachJitWorkaround::HandleMachError(int error, std::string operation) {
	if (error != 0)
	{
		auto str = mach_error_string(error);
		printf("Mach operation '%s' failed with error: %s\n", operation.c_str(), str);
//		throw new InvalidOperationException($"Mach operation '{operation}' failed with error: {error}");
	}
}


#endif
