// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <utility>
#include <vector>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/program_metadata.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nca.h"
#include "core/loader/nso.h"
#include "core/memory.h"

namespace Loader {

AppLoader_NCA::AppLoader_NCA(FileSys::VirtualFile file_)
    : AppLoader(std::move(file_)), nca(std::make_unique<FileSys::NCA>(file)) {}

FileType AppLoader_NCA::IdentifyType(const FileSys::VirtualFile& file) {
    FileSys::NCA nca(file);

    if (nca.GetStatus() == ResultStatus::Success &&
        nca.GetType() == FileSys::NCAContentType::Program)
        return FileType::NCA;

    return FileType::Error;
}

ResultStatus AppLoader_NCA::Load(Kernel::SharedPtr<Kernel::Process>& process) {
    if (is_loaded) {
        return ResultStatus::ErrorAlreadyLoaded;
    }

    const auto result = nca->GetStatus();
    if (result != ResultStatus::Success) {
        return result;
    }

    if (nca->GetType() != FileSys::NCAContentType::Program)
        return ResultStatus::ErrorInvalidFormat;

    const auto exefs = nca->GetExeFS();

    if (exefs == nullptr)
        return ResultStatus::ErrorInvalidFormat;

    directory_loader = std::make_unique<AppLoader_DeconstructedRomDirectory>(exefs);

    const auto load_result = directory_loader->Load(process);
    if (load_result != ResultStatus::Success)
        return load_result;

    if (nca->GetRomFS() != nullptr && nca->GetRomFS()->GetSize() > 0)
        Service::FileSystem::RegisterRomFS(std::make_unique<FileSys::RomFSFactory>(*this));

    is_loaded = true;

    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadRomFS(FileSys::VirtualFile& dir) {
    if (nca == nullptr)
        return ResultStatus::ErrorNotLoaded;
    if (nca->GetRomFS() == nullptr || nca->GetRomFS()->GetSize() == 0)
        return ResultStatus::ErrorNotUsed;
    dir = nca->GetRomFS();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadProgramId(u64& out_program_id) {
    if (nca == nullptr || nca->GetStatus() != ResultStatus::Success)
        return ResultStatus::ErrorInvalidFormat;
    out_program_id = nca->GetTitleId();
    return ResultStatus::Success;
}

AppLoader_NCA::~AppLoader_NCA() = default;

} // namespace Loader
