/*
 * Unified file-operations contract executor
 * src/core/file_ops_contract.cpp
 */

#include "file_ops_contract.h"

#include "fs_ops.h"
#include "linux_fs_safety.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>

namespace PCManFM::FileOpsContract {
namespace {

struct SourceStats {
    std::uint64_t bytesTotal = 0;
    int entryCount = 0;
};

struct SourcePlan {
    std::string sourcePath;
    std::string destinationPath;
    SourceStats stats;
    int workUnits = 1;
};

bool is_copy_like(Operation operation) {
    return operation == Operation::Copy || operation == Operation::Move;
}

bool is_cancel_requested(const Request& request) {
    return request.cancellationRequested && request.cancellationRequested();
}

void set_error(Error& out, EngineErrorCode code, OperationStep step, int sys_errno, const std::string& message) {
    out.code = code;
    out.step = step;
    out.sysErrno = sys_errno;
    out.message = message;
}

void set_errno_error(Error& out, EngineErrorCode code, OperationStep step, int sys_errno, const char* context) {
    set_error(out, code, step, sys_errno, std::string(context) + ": " + std::string(std::strerror(sys_errno)));
}

void set_cancelled(Error& out, OperationStep step, const std::string& message = std::string()) {
    set_error(out, EngineErrorCode::Cancelled, step, ECANCELED,
              message.empty() ? std::string("Operation cancelled") : message);
}

void map_fs_error(const FsOps::Error& fs_err, OperationStep step, Error& out) {
    const EngineErrorCode code =
        (fs_err.code == ENOSYS) ? EngineErrorCode::SafetyRequirementUnavailable : EngineErrorCode::OperationFailed;
    set_error(out, code, step, fs_err.code, fs_err.message);
}

void add_u64_saturated(std::uint64_t& dst, std::uint64_t value) {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    dst = (value > max - dst) ? max : (dst + value);
}

void add_int_saturated(int& dst, int value) {
    if (value <= 0) {
        return;
    }

    const int max = std::numeric_limits<int>::max();
    dst = (value > max - dst) ? max : (dst + value);
}

int sum_int_saturated(int lhs, int rhs) {
    int out = lhs;
    add_int_saturated(out, rhs);
    return out;
}

std::uint64_t sum_u64_saturated(std::uint64_t lhs, std::uint64_t rhs) {
    std::uint64_t out = lhs;
    add_u64_saturated(out, rhs);
    return out;
}

std::string path_basename(std::string path) {
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if (path.empty()) {
        return std::string();
    }

    const std::size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

std::string join_path(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    if (parent.back() == '/') {
        return parent + child;
    }
    return parent + "/" + child;
}

bool scan_path_stats(const std::string& path, SourceStats& stats, Error& err, int depth = 0) {
    if (depth > FsOps::kMaxRecursionDepth) {
        set_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, ELOOP,
                  "Maximum recursion depth exceeded");
        return false;
    }

    struct stat st{};
    if (::lstat(path.c_str(), &st) < 0) {
        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, errno, "lstat");
        return false;
    }

    add_int_saturated(stats.entryCount, 1);
    if (S_ISREG(st.st_mode)) {
        add_u64_saturated(stats.bytesTotal, static_cast<std::uint64_t>(st.st_size));
    }

    if (!S_ISDIR(st.st_mode)) {
        return true;
    }

    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, errno, "opendir");
        return false;
    }

    for (;;) {
        errno = 0;
        dirent* ent = ::readdir(dir);
        if (!ent) {
            if (errno != 0) {
                const int errnum = errno;
                ::closedir(dir);
                set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, errnum, "readdir");
                return false;
            }
            break;
        }

        const char* child = ent->d_name;
        if (!child || child[0] == '\0' || std::strcmp(child, ".") == 0 || std::strcmp(child, "..") == 0) {
            continue;
        }

        if (!scan_path_stats(join_path(path, child), stats, err, depth + 1)) {
            ::closedir(dir);
            return false;
        }
    }

    ::closedir(dir);
    return true;
}

bool probe_openat2_resolve(Error& err) {
    FsOps::Error probe_err;
    LinuxFsSafety::Fd probe_fd;
    if (LinuxFsSafety::open_under(AT_FDCWD, ".", LinuxFsSafety::open_dir_flags(), 0, LinuxFsSafety::kResolveNoSymlinks,
                                  probe_fd, probe_err)) {
        return true;
    }

    const EngineErrorCode code =
        (probe_err.code == ENOSYS) ? EngineErrorCode::SafetyRequirementUnavailable : EngineErrorCode::OperationFailed;
    set_error(err, code, OperationStep::ValidateRequest, probe_err.code,
              probe_err.message.empty() ? std::string("openat2 probe failed") : probe_err.message);
    return false;
}

bool validate_request(const Request& request, Error& err) {
    if (request.sources.empty()) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                  "Request has no sources");
        return false;
    }

    if (!request.metadata.preservePermissions || !request.metadata.preserveTimestamps) {
        set_error(err, EngineErrorCode::UnsupportedFeature, OperationStep::ValidateRequest, ENOTSUP,
                  "Disabling permission/timestamp preservation is not supported by the current engine");
        return false;
    }

    if (request.symlinkPolicy.followSymlinks || request.symlinkPolicy.copyMode != SymlinkCopyMode::CopyLinkAsLink) {
        set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                  "Only no-follow symlink policy with link-as-link copy mode is currently supported");
        return false;
    }

    if (request.atomicity.requireAtomicReplace) {
        set_error(err, EngineErrorCode::UnsupportedFeature, OperationStep::ValidateRequest, ENOTSUP,
                  "requireAtomicReplace is not yet supported for file operations");
        return false;
    }

    if (request.operation == Operation::Move && !request.atomicity.bestEffortAtomicMove) {
        set_error(err, EngineErrorCode::UnsupportedFeature, OperationStep::ValidateRequest, ENOTSUP,
                  "Disabling best-effort atomic move is not supported by the current engine");
        return false;
    }

    if (request.conflictPolicy == ConflictPolicy::Rename) {
        set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                  "Rename conflict policy is not yet supported");
        return false;
    }

    if (request.linuxSafety.requireLandlock) {
        set_error(err, EngineErrorCode::SafetyRequirementUnavailable, OperationStep::ValidateRequest, ENOSYS,
                  "Landlock was requested but is not available in this build");
        return false;
    }

    if (request.linuxSafety.requireOpenat2Resolve && !probe_openat2_resolve(err)) {
        return false;
    }

    switch (request.operation) {
        case Operation::Copy:
        case Operation::Move:
            if (request.destination.targetDir.empty()) {
                set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                          "Destination target directory is required");
                return false;
            }
            if (request.destination.mappingMode == DestinationMappingMode::ExplicitPerSource &&
                request.destination.explicitTargets.size() != request.sources.size()) {
                set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                          "explicitTargets must match source count");
                return false;
            }
            break;
        case Operation::Delete:
            break;
        case Operation::Mkdir:
        case Operation::Link:
            set_error(err, EngineErrorCode::UnsupportedOperation, OperationStep::ValidateRequest, ENOTSUP,
                      "Operation is not yet supported by the unified contract executor");
            return false;
    }

    return true;
}

bool build_plan(const Request& request, std::vector<SourcePlan>& plans, ProgressSnapshot& totals, Error& err) {
    plans.clear();
    totals = {};

    plans.reserve(request.sources.size());

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        const std::string& source = request.sources[index];
        if (source.empty()) {
            set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                      "Source path must not be empty");
            return false;
        }

        SourceStats stats;
        if (!scan_path_stats(source, stats, err)) {
            return false;
        }

        SourcePlan plan;
        plan.sourcePath = source;
        plan.stats = stats;
        plan.workUnits = (request.operation == Operation::Delete) ? std::max(1, stats.entryCount) : 1;

        if (is_copy_like(request.operation)) {
            if (request.destination.mappingMode == DestinationMappingMode::ExplicitPerSource) {
                plan.destinationPath = request.destination.explicitTargets[index];
            }
            else {
                const std::string file_name = path_basename(source);
                if (file_name.empty() || file_name == "." || file_name == "..") {
                    set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                              "Source basename is invalid");
                    return false;
                }
                plan.destinationPath = join_path(request.destination.targetDir, file_name);
            }
        }

        add_int_saturated(totals.filesTotal, plan.workUnits);
        add_u64_saturated(totals.bytesTotal, plan.stats.bytesTotal);

        plans.push_back(std::move(plan));
    }

    return true;
}

void emit_monotonic_progress(const ProgressSnapshot& candidate,
                             ProgressSnapshot& state,
                             const EventHandlers& handlers) {
    ProgressSnapshot next = candidate;

    next.filesTotal = state.filesTotal;
    next.bytesTotal = state.bytesTotal;
    next.filesDone = std::min(next.filesDone, next.filesTotal);
    next.bytesDone = std::min(next.bytesDone, next.bytesTotal);
    next.filesDone = std::max(next.filesDone, state.filesDone);
    next.bytesDone = std::max(next.bytesDone, state.bytesDone);

    if (next.currentPath.empty()) {
        next.currentPath = state.currentPath;
    }

    state = next;
    if (handlers.onProgress) {
        handlers.onProgress(state);
    }
}

bool destination_exists(const std::string& path, bool& exists, bool& is_dir, Error& err) {
    exists = false;
    is_dir = false;

    struct stat st{};
    if (::lstat(path.c_str(), &st) == 0) {
        exists = true;
        is_dir = S_ISDIR(st.st_mode);
        return true;
    }

    if (errno == ENOENT) {
        return true;
    }

    set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, errno, "lstat");
    return false;
}

void mark_plan_complete_without_execution(const SourcePlan& plan,
                                          const ProgressSnapshot& totals,
                                          int& completed_units,
                                          std::uint64_t& completed_bytes,
                                          ProgressSnapshot& last_progress,
                                          const EventHandlers& handlers) {
    add_int_saturated(completed_units, plan.workUnits);
    add_u64_saturated(completed_bytes, plan.stats.bytesTotal);

    ProgressSnapshot skipped_progress;
    skipped_progress.filesTotal = totals.filesTotal;
    skipped_progress.bytesTotal = totals.bytesTotal;
    skipped_progress.filesDone = completed_units;
    skipped_progress.bytesDone = completed_bytes;
    skipped_progress.currentPath = plan.sourcePath;
    emit_monotonic_progress(skipped_progress, last_progress, handlers);
}

bool resolve_conflict_for_plan(const Request& request,
                               const EventHandlers& handlers,
                               const SourcePlan& plan,
                               bool& skip,
                               bool& allow_overwrite,
                               Error& err) {
    skip = false;
    allow_overwrite = true;
    if (!is_copy_like(request.operation)) {
        return true;
    }

    bool exists = false;
    bool is_dir = false;
    if (!destination_exists(plan.destinationPath, exists, is_dir, err)) {
        return false;
    }
    if (!exists) {
        switch (request.conflictPolicy) {
            case ConflictPolicy::Overwrite:
                allow_overwrite = true;
                return true;
            case ConflictPolicy::Skip:
                // Enforce skip semantics for late destination races.
                allow_overwrite = false;
                return true;
            case ConflictPolicy::Prompt:
                // Defer decision until an actual conflict exists.
                allow_overwrite = false;
                return true;
            case ConflictPolicy::Rename:
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                          "Rename conflict policy is not yet supported");
                return false;
        }
        return true;
    }

    ConflictResolution resolution = ConflictResolution::Overwrite;
    switch (request.conflictPolicy) {
        case ConflictPolicy::Overwrite:
            resolution = ConflictResolution::Overwrite;
            break;
        case ConflictPolicy::Skip:
            resolution = ConflictResolution::Skip;
            break;
        case ConflictPolicy::Prompt:
            if (!handlers.onConflict) {
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                          "Prompt conflict policy requires an onConflict handler");
                return false;
            }
            resolution = handlers.onConflict(ConflictEvent{
                ConflictKind::DestinationExists,
                plan.sourcePath,
                plan.destinationPath,
                is_dir,
            });
            break;
        case ConflictPolicy::Rename:
            set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                      "Rename conflict policy is not yet supported");
            return false;
    }

    switch (resolution) {
        case ConflictResolution::Overwrite:
            skip = false;
            allow_overwrite = true;
            return true;
        case ConflictResolution::Skip:
            skip = true;
            allow_overwrite = false;
            return true;
        case ConflictResolution::Abort:
            set_error(err, EngineErrorCode::ConflictAborted, OperationStep::ResolveConflict, ECANCELED,
                      "Operation aborted during conflict resolution");
            return false;
        case ConflictResolution::Rename:
            set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                      "Rename conflict resolution is not yet supported");
            return false;
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EINVAL,
              "Unknown conflict resolution decision");
    return false;
}

bool execute_plan_operation(const Request& request,
                            const SourcePlan& plan,
                            FsOps::ProgressInfo& source_progress,
                            const FsOps::ProgressCallback& callback,
                            FsOps::Error& fs_err,
                            bool overwrite_existing) {
    switch (request.operation) {
        case Operation::Copy:
            return FsOps::copy_path(plan.sourcePath, plan.destinationPath, source_progress, callback, fs_err,
                                    request.metadata.preserveOwnership, overwrite_existing);
        case Operation::Move:
            return FsOps::move_path(plan.sourcePath, plan.destinationPath, source_progress, callback, fs_err,
                                    /*forceCopyFallbackForTests=*/false, request.metadata.preserveOwnership,
                                    overwrite_existing);
        case Operation::Delete:
            return FsOps::delete_path(plan.sourcePath, source_progress, callback, fs_err);
        case Operation::Mkdir:
        case Operation::Link:
            fs_err.code = ENOTSUP;
            fs_err.message = "Operation is not supported";
            return false;
    }

    fs_err.code = EINVAL;
    fs_err.message = "Unknown operation";
    return false;
}

}  // namespace

Result run(const Request& request, const EventHandlers& handlers) {
    Result result;

    if (is_cancel_requested(request)) {
        result.cancelled = true;
        set_cancelled(result.error, OperationStep::ValidateRequest);
        return result;
    }

    if (!validate_request(request, result.error)) {
        return result;
    }

    std::vector<SourcePlan> plans;
    ProgressSnapshot totals;
    if (!build_plan(request, plans, totals, result.error)) {
        return result;
    }

    ProgressSnapshot last_progress;
    last_progress.filesTotal = totals.filesTotal;
    last_progress.bytesTotal = totals.bytesTotal;

    int completed_units = 0;
    std::uint64_t completed_bytes = 0;

    for (const SourcePlan& plan : plans) {
        if (is_cancel_requested(request)) {
            result.cancelled = true;
            set_cancelled(result.error, OperationStep::Execute);
            result.finalProgress = last_progress;
            return result;
        }

        bool skip = false;
        bool allow_overwrite = true;
        Error conflict_error;
        if (!resolve_conflict_for_plan(request, handlers, plan, skip, allow_overwrite, conflict_error)) {
            if (conflict_error.code == EngineErrorCode::ConflictAborted || conflict_error.sysErrno == ECANCELED) {
                result.cancelled = true;
                set_cancelled(result.error, OperationStep::ResolveConflict, conflict_error.message);
            }
            else {
                result.error = conflict_error;
            }
            result.finalProgress = last_progress;
            return result;
        }

        if (skip) {
            mark_plan_complete_without_execution(plan, totals, completed_units, completed_bytes, last_progress,
                                                 handlers);
            continue;
        }

        FsOps::ProgressInfo source_progress;
        auto reset_source_progress = [&source_progress, &plan]() {
            source_progress = {};
            source_progress.filesTotal = plan.workUnits;
            source_progress.filesDone = 0;
            source_progress.bytesTotal = plan.stats.bytesTotal;
            source_progress.bytesDone = 0;
            source_progress.currentPath = plan.sourcePath;
        };
        reset_source_progress();

        const auto progress_cb = [&request, &handlers, &plan, completed_units, completed_bytes, &totals,
                                  &last_progress](const FsOps::ProgressInfo& source_info) {
            if (is_cancel_requested(request)) {
                return false;
            }

            const int local_done = std::clamp(source_info.filesDone, 0, plan.workUnits);
            const std::uint64_t local_bytes_done = std::min(source_info.bytesDone, plan.stats.bytesTotal);

            ProgressSnapshot aggregate;
            aggregate.filesTotal = totals.filesTotal;
            aggregate.bytesTotal = totals.bytesTotal;
            aggregate.filesDone = sum_int_saturated(completed_units, local_done);
            aggregate.bytesDone = sum_u64_saturated(completed_bytes, local_bytes_done);
            aggregate.currentPath = source_info.currentPath.empty() ? plan.sourcePath : source_info.currentPath;

            emit_monotonic_progress(aggregate, last_progress, handlers);
            return !is_cancel_requested(request);
        };

        FsOps::Error fs_err;
        bool executed = execute_plan_operation(request, plan, source_progress, progress_cb, fs_err, allow_overwrite);
        if (!executed && is_copy_like(request.operation) && fs_err.code == EEXIST && !allow_overwrite) {
            bool late_skip = false;
            bool late_allow_overwrite = false;
            Error late_conflict_error;
            if (!resolve_conflict_for_plan(request, handlers, plan, late_skip, late_allow_overwrite,
                                           late_conflict_error)) {
                if (late_conflict_error.code == EngineErrorCode::ConflictAborted ||
                    late_conflict_error.sysErrno == ECANCELED) {
                    result.cancelled = true;
                    set_cancelled(result.error, OperationStep::ResolveConflict, late_conflict_error.message);
                }
                else {
                    result.error = late_conflict_error;
                }
                result.finalProgress = last_progress;
                return result;
            }

            if (late_skip) {
                mark_plan_complete_without_execution(plan, totals, completed_units, completed_bytes, last_progress,
                                                     handlers);
                continue;
            }

            if (late_allow_overwrite) {
                reset_source_progress();
                fs_err = {};
                executed = execute_plan_operation(request, plan, source_progress, progress_cb, fs_err, true);
            }
        }

        if (!executed) {
            if (is_cancel_requested(request) || fs_err.code == ECANCELED) {
                result.cancelled = true;
                set_cancelled(result.error, OperationStep::Execute,
                              fs_err.message.empty() ? std::string("Operation cancelled") : fs_err.message);
            }
            else {
                map_fs_error(fs_err, OperationStep::Execute, result.error);
            }
            result.finalProgress = last_progress;
            return result;
        }

        add_int_saturated(completed_units, plan.workUnits);
        add_u64_saturated(completed_bytes, plan.stats.bytesTotal);

        ProgressSnapshot done_progress;
        done_progress.filesTotal = totals.filesTotal;
        done_progress.bytesTotal = totals.bytesTotal;
        done_progress.filesDone = completed_units;
        done_progress.bytesDone = completed_bytes;
        done_progress.currentPath = plan.sourcePath;
        emit_monotonic_progress(done_progress, last_progress, handlers);
    }

    result.success = true;
    result.finalProgress = last_progress;
    return result;
}

}  // namespace PCManFM::FileOpsContract
