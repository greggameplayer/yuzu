// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <vector>
#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/memory.h"
#include "core/memory_hook.h"

namespace FileSys {
enum class ProgramAddressSpaceType : u8;
}

namespace Kernel {

// Checks if address + size is greater than the given address
// This can return false if the size causes an overflow of a 64-bit type
// or if the given size is zero.
constexpr bool IsValidAddressRange(VAddr address, u64 size) {
    return address + size > address;
}

// Checks if a given address range lies within a larger address range.
constexpr bool IsInsideAddressRange(VAddr address, u64 size, VAddr address_range_begin,
                                    VAddr address_range_end) {
    const VAddr end_address = address + size - 1;
    return address_range_begin <= address && end_address <= address_range_end - 1;
}

enum class VMAType : u8 {
    /// VMA represents an unmapped region of the address space.
    Free,
    /// VMA is backed by a ref-counted allocate memory block.
    AllocatedMemoryBlock,
    /// VMA is backed by a raw, unmanaged pointer.
    BackingMemory,
    /// VMA is mapped to MMIO registers at a fixed PAddr.
    MMIO,
    // TODO(yuriks): Implement MemoryAlias to support MAP/UNMAP
};

/// Permissions for mapped memory blocks
enum class VMAPermission : u8 {
    None = 0,
    Read = 1,
    Write = 2,
    Execute = 4,

    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    WriteExecute = Write | Execute,
    ReadWriteExecute = Read | Write | Execute,
};

// clang-format off
/// Represents memory states and any relevant flags, as used by the kernel.
/// svcQueryMemory interprets these by masking away all but the first eight
/// bits when storing memory state into a MemoryInfo instance.
enum class MemoryState : u32 {
    Mask                            = 0xFF,
    FlagProtect                     = 1U << 8,
    FlagDebug                       = 1U << 9,
    FlagIPC0                        = 1U << 10,
    FlagIPC3                        = 1U << 11,
    FlagIPC1                        = 1U << 12,
    FlagMapped                      = 1U << 13,
    FlagCode                        = 1U << 14,
    FlagAlias                       = 1U << 15,
    FlagModule                      = 1U << 16,
    FlagTransfer                    = 1U << 17,
    FlagQueryPhysicalAddressAllowed = 1U << 18,
    FlagSharedDevice                = 1U << 19,
    FlagSharedDeviceAligned         = 1U << 20,
    FlagIPCBuffer                   = 1U << 21,
    FlagMemoryPoolAllocated         = 1U << 22,
    FlagMapProcess                  = 1U << 23,
    FlagUncached                    = 1U << 24,
    FlagCodeMemory                  = 1U << 25,

    // Convenience flag sets to reduce repetition
    IPCFlags = FlagIPC0 | FlagIPC3 | FlagIPC1,

    CodeFlags = FlagDebug | IPCFlags | FlagMapped | FlagCode | FlagQueryPhysicalAddressAllowed |
                FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    DataFlags = FlagProtect | IPCFlags | FlagMapped | FlagAlias | FlagTransfer |
                FlagQueryPhysicalAddressAllowed | FlagSharedDevice | FlagSharedDeviceAligned |
                FlagMemoryPoolAllocated | FlagIPCBuffer | FlagUncached,

    Unmapped               = 0x00,
    Io                     = 0x01 | FlagMapped,
    Normal                 = 0x02 | FlagMapped | FlagQueryPhysicalAddressAllowed,
    CodeStatic             = 0x03 | CodeFlags  | FlagMapProcess,
    CodeMutable            = 0x04 | CodeFlags  | FlagMapProcess | FlagCodeMemory,
    Heap                   = 0x05 | DataFlags  | FlagCodeMemory,
    Shared                 = 0x06 | FlagMapped | FlagMemoryPoolAllocated,
    ModuleCodeStatic       = 0x08 | CodeFlags  | FlagModule | FlagMapProcess,
    ModuleCodeMutable      = 0x09 | DataFlags  | FlagModule | FlagMapProcess | FlagCodeMemory,

    IpcBuffer0             = 0x0A | FlagMapped | FlagQueryPhysicalAddressAllowed | FlagMemoryPoolAllocated |
                                    IPCFlags | FlagSharedDevice | FlagSharedDeviceAligned,

    Stack                  = 0x0B | FlagMapped | IPCFlags | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    ThreadLocal            = 0x0C | FlagMapped | FlagMemoryPoolAllocated,

    TransferMemoryIsolated = 0x0D | IPCFlags | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated |
                                    FlagUncached,

    TransferMemory         = 0x0E | FlagIPC3   | FlagIPC1   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    ProcessMemory          = 0x0F | FlagIPC3   | FlagIPC1   | FlagMapped | FlagMemoryPoolAllocated,

    // Used to signify an inaccessible or invalid memory region with memory queries
    Inaccessible           = 0x10,

    IpcBuffer1             = 0x11 | FlagIPC3   | FlagIPC1   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    IpcBuffer3             = 0x12 | FlagIPC3   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    KernelStack            = 0x13 | FlagMapped,
};
// clang-format on

constexpr MemoryState operator|(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) | u32(rhs));
}

constexpr MemoryState operator&(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) & u32(rhs));
}

constexpr MemoryState operator^(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) ^ u32(rhs));
}

constexpr MemoryState operator~(MemoryState lhs) {
    return static_cast<MemoryState>(~u32(lhs));
}

constexpr MemoryState& operator|=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr MemoryState& operator&=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr MemoryState& operator^=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

constexpr u32 ToSvcMemoryState(MemoryState state) {
    return static_cast<u32>(state & MemoryState::Mask);
}

struct MemoryInfo {
    u64 base_address;
    u64 size;
    u32 state;
    u32 attributes;
    u32 permission;
    u32 ipc_ref_count;
    u32 device_ref_count;
};
static_assert(sizeof(MemoryInfo) == 0x28, "MemoryInfo has incorrect size.");

struct PageInfo {
    u32 flags;
};

/**
 * Represents a VMA in an address space. A VMA is a contiguous region of virtual addressing space
 * with homogeneous attributes across its extents. In this particular implementation each VMA is
 * also backed by a single host memory allocation.
 */
struct VirtualMemoryArea {
    /// Virtual base address of the region.
    VAddr base = 0;
    /// Size of the region.
    u64 size = 0;

    VMAType type = VMAType::Free;
    VMAPermission permissions = VMAPermission::None;
    /// Tag returned by svcQueryMemory. Not otherwise used.
    MemoryState meminfo_state = MemoryState::Unmapped;

    // Settings for type = AllocatedMemoryBlock
    /// Memory block backing this VMA.
    std::shared_ptr<std::vector<u8>> backing_block = nullptr;
    /// Offset into the backing_memory the mapping starts from.
    std::size_t offset = 0;

    // Settings for type = BackingMemory
    /// Pointer backing this VMA. It will not be destroyed or freed when the VMA is removed.
    u8* backing_memory = nullptr;

    // Settings for type = MMIO
    /// Physical address of the register area this VMA maps to.
    PAddr paddr = 0;
    Memory::MemoryHookPointer mmio_handler = nullptr;

    /// Tests if this area can be merged to the right with `next`.
    bool CanBeMergedWith(const VirtualMemoryArea& next) const;
};

/**
 * Manages a process' virtual addressing space. This class maintains a list of allocated and free
 * regions in the address space, along with their attributes, and allows kernel clients to
 * manipulate it, adjusting the page table to match.
 *
 * This is similar in idea and purpose to the VM manager present in operating system kernels, with
 * the main difference being that it doesn't have to support swapping or memory mapping of files.
 * The implementation is also simplified by not having to allocate page frames. See these articles
 * about the Linux kernel for an explantion of the concept and implementation:
 *  - http://duartes.org/gustavo/blog/post/how-the-kernel-manages-your-memory/
 *  - http://duartes.org/gustavo/blog/post/page-cache-the-affair-between-memory-and-files/
 */
class VMManager final {
    using VMAMap = std::map<VAddr, VirtualMemoryArea>;

public:
    using VMAHandle = VMAMap::const_iterator;

    VMManager();
    ~VMManager();

    /// Clears the address space map, re-initializing with a single free area.
    void Reset(FileSys::ProgramAddressSpaceType type);

    /// Finds the VMA in which the given address is included in, or `vma_map.end()`.
    VMAHandle FindVMA(VAddr target) const;

    /// Indicates whether or not the given handle is within the VMA map.
    bool IsValidHandle(VMAHandle handle) const;

    // TODO(yuriks): Should these functions actually return the handle?

    /**
     * Maps part of a ref-counted block of memory at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param block The block to be mapped.
     * @param offset Offset into `block` to map from.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     */
    ResultVal<VMAHandle> MapMemoryBlock(VAddr target, std::shared_ptr<std::vector<u8>> block,
                                        std::size_t offset, u64 size, MemoryState state);

    /**
     * Maps an unmanaged host memory pointer at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param memory The memory to be mapped.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     */
    ResultVal<VMAHandle> MapBackingMemory(VAddr target, u8* memory, u64 size, MemoryState state);

    /**
     * Finds the first free address that can hold a region of the desired size.
     *
     * @param size Size of the desired region.
     * @return The found free address.
     */
    ResultVal<VAddr> FindFreeRegion(u64 size) const;

    /**
     * Maps memory to the PersonalMmHeap region at a given address. MapPhysicalMemory will not remap
     * any regions. The goal of MapPhysicalMemory is to "fill" a regions empty space given an offset
     * and a size. Any memory which is already mapped in the subsection we want to allocate is
     * ignored and we only map the remaining data needed. Reminder that we're not remapping, just
     * filling the space we want to fill. This is typically used with "PersonalMmHeap" which allows
     * processes to have extra resources mapped. Typically this is seen with 5.0.0+ games and
     * sysmodule specifically. The PersonalMmHeapSize is pulled from the NPDM and is passed to
     * loader when the process is created which is in turn passed to the kernel when
     * svcCreateProcess is called
     *
     * @param target The address of where you want to map
     * @param size The size of the memory you want to map
     */
    ResultCode MapPhysicalMemory(VAddr target, u64 size);

    /**
     * Unmaps memory from the PersonalMmHeap region at a given address.
     *
     * @param target The address of where you want to unmap
     * @param size The size of the memory you want to unmap
     */
    ResultCode UnmapPhysicalMemory(VAddr target, u64 size);

    /**
     * Maps a memory-mapped IO region at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param paddr The physical address where the registers are present.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     * @param mmio_handler The handler that will implement read and write for this MMIO region.
     */
    ResultVal<VMAHandle> MapMMIO(VAddr target, PAddr paddr, u64 size, MemoryState state,
                                 Memory::MemoryHookPointer mmio_handler);

    /// Unmaps a range of addresses, splitting VMAs as necessary.
    ResultCode UnmapRange(VAddr target, u64 size);

    /// Changes the permissions of the given VMA.
    VMAHandle Reprotect(VMAHandle vma, VMAPermission new_perms);

    /// Changes the permissions of a range of addresses, splitting VMAs as necessary.
    ResultCode ReprotectRange(VAddr target, u64 size, VMAPermission new_perms);

    ResultVal<VAddr> HeapAllocate(VAddr target, u64 size, VMAPermission perms);
    ResultCode HeapFree(VAddr target, u64 size);

    ResultCode MirrorMemory(VAddr dst_addr, VAddr src_addr, u64 size, MemoryState state);

    /// Queries the memory manager for information about the given address.
    ///
    /// @param address The address to query the memory manager about for information.
    ///
    /// @return A MemoryInfo instance containing information about the given address.
    ///
    MemoryInfo QueryMemory(VAddr address) const;

    /**
     * Scans all VMAs and updates the page table range of any that use the given vector as backing
     * memory. This should be called after any operation that causes reallocation of the vector.
     */
    void RefreshMemoryBlockMappings(const std::vector<u8>* block);

    /// Dumps the address space layout to the log, for debugging
    void LogLayout() const;

    /// Gets the total memory usage, used by svcGetInfo
    u64 GetTotalMemoryUsage() const;

    /// Gets the total heap usage, used by svcGetInfo
    u64 GetTotalHeapUsage() const;

    /// Gets the address space base address
    VAddr GetAddressSpaceBaseAddress() const;

    /// Gets the address space end address
    VAddr GetAddressSpaceEndAddress() const;

    /// Gets the total address space address size in bytes
    u64 GetAddressSpaceSize() const;

    /// Gets the address space width in bits.
    u64 GetAddressSpaceWidth() const;

    /// Gets the base address of the ASLR region.
    VAddr GetASLRRegionBaseAddress() const;

    /// Gets the end address of the ASLR region.
    VAddr GetASLRRegionEndAddress() const;

    /// Determines whether or not the specified address range is within the ASLR region.
    bool IsWithinASLRRegion(VAddr address, u64 size) const;

    /// Gets the size of the ASLR region
    u64 GetASLRRegionSize() const;

    /// Gets the base address of the code region.
    VAddr GetCodeRegionBaseAddress() const;

    /// Gets the end address of the code region.
    VAddr GetCodeRegionEndAddress() const;

    /// Gets the total size of the code region in bytes.
    u64 GetCodeRegionSize() const;

    /// Gets the base address of the heap region.
    VAddr GetHeapRegionBaseAddress() const;

    /// Gets the end address of the heap region;
    VAddr GetHeapRegionEndAddress() const;

    /// Gets the total size of the heap region in bytes.
    u64 GetHeapRegionSize() const;

    /// Gets the base address of the map region.
    VAddr GetMapRegionBaseAddress() const;

    /// Gets the end address of the map region.
    VAddr GetMapRegionEndAddress() const;

    /// Gets the total size of the map region in bytes.
    u64 GetMapRegionSize() const;

    /// Gets the base address of the new map region.
    VAddr GetNewMapRegionBaseAddress() const;

    /// Gets the end address of the new map region.
    VAddr GetNewMapRegionEndAddress() const;

    /// Gets the total size of the new map region in bytes.
    u64 GetNewMapRegionSize() const;

    /// Gets the base address of the TLS IO region.
    VAddr GetTLSIORegionBaseAddress() const;

    /// Gets the end address of the TLS IO region.
    VAddr GetTLSIORegionEndAddress() const;

    /// Gets the total size of the TLS IO region in bytes.
    u64 GetTLSIORegionSize() const;

    /// Gets the total size of the PersonalMmHeap region in bytes.
    u64 GetPersonalMmHeapUsage() const;

    bool IsInsideAddressSpace(VAddr address, u64 size) const;
    bool IsInsideNewMapRegion(VAddr address, u64 size) const;
    bool IsInsideMapRegion(VAddr address, u64 size) const;

    /// Each VMManager has its own page table, which is set as the main one when the owning process
    /// is scheduled.
    Memory::PageTable page_table;

private:
    using VMAIter = VMAMap::iterator;

    /// Converts a VMAHandle to a mutable VMAIter.
    VMAIter StripIterConstness(const VMAHandle& iter);

    /// Unmaps the given VMA.
    VMAIter Unmap(VMAIter vma);

    /**
     * Carves a VMA of a specific size at the specified address by splitting Free VMAs while doing
     * the appropriate error checking.
     */
    ResultVal<VMAIter> CarveVMA(VAddr base, u64 size);

    /**
     * Splits the edges of the given range of non-Free VMAs so that there is a VMA split at each
     * end of the range.
     */
    ResultVal<VMAIter> CarveVMARange(VAddr base, u64 size);

    /**
     * Splits a VMA in two, at the specified offset.
     * @returns the right side of the split, with the original iterator becoming the left side.
     */
    VMAIter SplitVMA(VMAIter vma, u64 offset_in_vma);

    /**
     * Checks for and merges the specified VMA with adjacent ones if possible.
     * @returns the merged VMA or the original if no merging was possible.
     */
    VMAIter MergeAdjacent(VMAIter vma);

    /// Updates the pages corresponding to this VMA so they match the VMA's attributes.
    void UpdatePageTableForVMA(const VirtualMemoryArea& vma);

    /// Initializes memory region ranges to adhere to a given address space type.
    void InitializeMemoryRegionRanges(FileSys::ProgramAddressSpaceType type);

    /// Clears the underlying map and page table.
    void Clear();

    /// Clears out the VMA map, unmapping any previously mapped ranges.
    void ClearVMAMap();

    /// Clears out the page table
    void ClearPageTable();

    /**
     * A map covering the entirety of the managed address space, keyed by the `base` field of each
     * VMA. It must always be modified by splitting or merging VMAs, so that the invariant
     * `elem.base + elem.size == next.base` is preserved, and mergeable regions must always be
     * merged when possible so that no two similar and adjacent regions exist that have not been
     * merged.
     */
    VMAMap vma_map;

    u32 address_space_width = 0;
    VAddr address_space_base = 0;
    VAddr address_space_end = 0;

    VAddr aslr_region_base = 0;
    VAddr aslr_region_end = 0;

    VAddr code_region_base = 0;
    VAddr code_region_end = 0;

    VAddr heap_region_base = 0;
    VAddr heap_region_end = 0;

    VAddr map_region_base = 0;
    VAddr map_region_end = 0;

    VAddr new_map_region_base = 0;
    VAddr new_map_region_end = 0;

    VAddr tls_io_region_base = 0;
    VAddr tls_io_region_end = 0;

    // Memory used to back the allocations in the regular heap. A single vector is used to cover
    // the entire virtual address space extents that bound the allocations, including any holes.
    // This makes deallocation and reallocation of holes fast and keeps process memory contiguous
    // in the emulator address space, allowing Memory::GetPointer to be reasonably safe.
    std::shared_ptr<std::vector<u8>> heap_memory;
    // The left/right bounds of the address space covered by heap_memory.
    VAddr heap_start = 0;
    VAddr heap_end = 0;
    u64 heap_used = 0;

    u64 personal_heap_usage = 0;
};
} // namespace Kernel
