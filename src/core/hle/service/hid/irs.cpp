// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/swap.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/hid/irs.h"

namespace Service::HID {

IRS::IRS() : ServiceFramework{"irs"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {302, &IRS::ActivateIrsensor, "ActivateIrsensor"},
        {303, &IRS::DeactivateIrsensor, "DeactivateIrsensor"},
        {304, &IRS::GetIrsensorSharedMemoryHandle, "GetIrsensorSharedMemoryHandle"},
        {305, &IRS::StopImageProcessor, "StopImageProcessor"},
        {306, &IRS::RunMomentProcessor, "RunMomentProcessor"},
        {307, &IRS::RunClusteringProcessor, "RunClusteringProcessor"},
        {308, &IRS::RunImageTransferProcessor, "RunImageTransferProcessor"},
        {309, &IRS::GetImageTransferProcessorState, "GetImageTransferProcessorState"},
        {310, &IRS::RunTeraPluginProcessor, "RunTeraPluginProcessor"},
        {311, &IRS::GetNpadIrCameraHandle, "GetNpadIrCameraHandle"},
        {312, &IRS::RunPointingProcessor, "RunPointingProcessor"},
        {313, &IRS::SuspendImageProcessor, "SuspendImageProcessor"},
        {314, &IRS::CheckFirmwareVersion, "CheckFirmwareVersion"},
        {315, &IRS::SetFunctionLevel, "SetFunctionLevel"},
        {316, &IRS::RunImageTransferExProcessor, "RunImageTransferExProcessor"},
        {317, &IRS::RunIrLedProcessor, "RunIrLedProcessor"},
        {318, &IRS::StopImageProcessorAsync, "StopImageProcessorAsync"},
        {319, &IRS::ActivateIrsensorWithFunctionLevel, "ActivateIrsensorWithFunctionLevel"},
    };
    // clang-format on

    RegisterHandlers(functions);

    auto& kernel = Core::System::GetInstance().Kernel();
    shared_mem = Kernel::SharedMemory::Create(
        kernel, nullptr, 0x8000, Kernel::MemoryPermission::ReadWrite,
        Kernel::MemoryPermission::Read, 0, Kernel::MemoryRegion::BASE, "IRS:SharedMemory");
}

void IRS::ActivateIrsensor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::DeactivateIrsensor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::GetIrsensorSharedMemoryHandle(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(shared_mem);
    LOG_DEBUG(Service_IRS, "called");
}

void IRS::StopImageProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunMomentProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunClusteringProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunImageTransferProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::GetImageTransferProcessorState(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 5};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<u64>(CoreTiming::GetTicks());
    rb.PushRaw<u32>(0);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunTeraPluginProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::GetNpadIrCameraHandle(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<u32>(device_handle);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunPointingProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::SuspendImageProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::CheckFirmwareVersion(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::SetFunctionLevel(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunImageTransferExProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::RunIrLedProcessor(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::StopImageProcessorAsync(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

void IRS::ActivateIrsensorWithFunctionLevel(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_IRS, "(STUBBED) called");
}

IRS::~IRS() = default;

IRS_SYS::IRS_SYS() : ServiceFramework{"irs:sys"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {500, &IRS_SYS::SetAppletResourceUserId, "SetAppletResourceUserId"},
        {501, nullptr, "RegisterAppletResourceUserId"},
        {502, nullptr, "UnregisterAppletResourceUserId"},
        {503, nullptr, "EnableAppletToGetInput"},
    };
    // clang-format on

    RegisterHandlers(functions);
}
AM::IWindowController IWindowController;

void IRS_SYS::SetAppletResourceUserId(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    IWindowController.applet_resource_user_id = rp.Pop<u64>();
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    LOG_DEBUG(Service_IRS, "called");
}

IRS_SYS::~IRS_SYS() = default;

} // namespace Service::HID
