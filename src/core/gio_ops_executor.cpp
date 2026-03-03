/*
 * GIO-backed file-operations executor for unified contract
 * src/core/gio_ops_executor.cpp
 */

#include "gio_ops_executor.h"

#include <gio/gio.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Oneg4FM::FileOpsContract::detail {
namespace {

struct SourceStats {
    std::uint64_t bytesTotal = 0;
    int entryCount = 0;
};

struct SourcePlan {
    std::string sourcePath;
    EndpointKind sourceKind = EndpointKind::NativePath;
    std::string destinationPath;
    EndpointKind destinationKind = EndpointKind::NativePath;
    SourceStats stats;
    int workUnits = 1;
};

struct ConflictResolutionState {
    bool hasApplyToAllResolution = false;
    ConflictResolution applyToAllResolution = ConflictResolution::Overwrite;
};

struct GObjectUnref {
    template <typename T>
    void operator()(T* object) const {
        if (object) {
            g_object_unref(object);
        }
    }
};

struct GErrorFree {
    void operator()(GError* err) const {
        if (err) {
            g_error_free(err);
        }
    }
};

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectUnref>;

using GErrorPtr = std::unique_ptr<GError, GErrorFree>;
using PerSourceProgressCallback =
    std::function<bool(int localFilesDone, std::uint64_t localBytesDone, const std::string& currentPath)>;

struct ByteProgressBridge {
    const PerSourceProgressCallback* callback = nullptr;
    std::uint64_t baseBytes = 0;
    std::string currentPath;
    GCancellable* cancellable = nullptr;
};

void gio_byte_progress(goffset currentBytes, goffset /*totalBytes*/, gpointer userData) {
    auto* bridge = static_cast<ByteProgressBridge*>(userData);
    if (!bridge || !bridge->callback) {
        return;
    }

    std::uint64_t current = 0;
    if (currentBytes > 0) {
        current = static_cast<std::uint64_t>(currentBytes);
    }

    if (!(*bridge->callback)(0, bridge->baseBytes + current, bridge->currentPath) && bridge->cancellable) {
        g_cancellable_cancel(bridge->cancellable);
    }
}

bool is_copy_like(Operation operation) {
    return operation == Operation::Copy || operation == Operation::Move;
}

bool operation_uses_destination(Operation operation) {
    return operation == Operation::Copy || operation == Operation::Move || operation == Operation::Untrash;
}

bool is_cancel_requested(const Request& request) {
    return request.cancellationRequested && request.cancellationRequested();
}

bool is_dot_or_dotdot(const std::string& name) {
    return name == "." || name == "..";
}

void set_error(Error& out, EngineErrorCode code, OperationStep step, int sysErrno, const std::string& message) {
    out.code = code;
    out.step = step;
    out.sysErrno = sysErrno;
    out.message = message;
}

void set_cancelled(Error& out, OperationStep step, const std::string& message = std::string()) {
    set_error(out, EngineErrorCode::Cancelled, step, ECANCELED,
              message.empty() ? std::string("Operation cancelled") : message);
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

void split_stem_and_extension(const std::string& fileName, std::string& stem, std::string& extension) {
    const std::size_t dot = fileName.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < fileName.size()) {
        stem = fileName.substr(0, dot);
        extension = fileName.substr(dot);
        return;
    }

    stem = fileName;
    extension.clear();
}

EndpointKind source_endpoint_kind(const Request& request, std::size_t index) {
    if (request.routing.sourceKinds.empty()) {
        return EndpointKind::NativePath;
    }
    return request.routing.sourceKinds[index];
}

EndpointKind infer_endpoint_kind(const std::string& endpoint) {
    return (endpoint.find("://") != std::string::npos) ? EndpointKind::Uri : EndpointKind::NativePath;
}

GObjectPtr<GFile> make_gfile(const std::string& endpoint, EndpointKind kind) {
    if (kind == EndpointKind::Uri) {
        return GObjectPtr<GFile>(g_file_new_for_uri(endpoint.c_str()));
    }
    return GObjectPtr<GFile>(g_file_new_for_path(endpoint.c_str()));
}

std::string endpoint_from_gfile(GFile* file, EndpointKind kind) {
    if (!file) {
        return {};
    }

    char* endpoint = nullptr;
    if (kind == EndpointKind::Uri) {
        endpoint = g_file_get_uri(file);
    }
    else {
        endpoint = g_file_get_path(file);
    }

    if (!endpoint) {
        endpoint = g_file_get_parse_name(file);
    }

    if (!endpoint) {
        return {};
    }

    std::string out(endpoint);
    g_free(endpoint);
    return out;
}

bool endpoint_basename(const std::string& endpoint, EndpointKind kind, std::string& basename, Error& err) {
    GObjectPtr<GFile> file = make_gfile(endpoint, kind);
    if (!file) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Unable to create endpoint handle for basename");
        return false;
    }

    char* raw = g_file_get_basename(file.get());
    if (!raw) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL, "Source basename is invalid");
        return false;
    }

    basename = raw;
    g_free(raw);

    if (basename.empty() || is_dot_or_dotdot(basename)) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL, "Source basename is invalid");
        return false;
    }

    return true;
}

bool build_child_endpoint(const std::string& parentEndpoint,
                          EndpointKind parentKind,
                          const std::string& childName,
                          std::string& outEndpoint,
                          Error& err) {
    GObjectPtr<GFile> parent = make_gfile(parentEndpoint, parentKind);
    if (!parent) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Unable to open destination parent endpoint");
        return false;
    }

    GObjectPtr<GFile> child(g_file_get_child(parent.get(), childName.c_str()));
    if (!child) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Unable to construct destination endpoint");
        return false;
    }

    outEndpoint = endpoint_from_gfile(child.get(), parentKind);
    if (outEndpoint.empty()) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Unable to encode destination endpoint");
        return false;
    }

    return true;
}

int errno_from_gio_error(const GError* err) {
    if (!err) {
        return EIO;
    }

    if (err->domain != G_IO_ERROR) {
        return EIO;
    }

    if (err->code == G_IO_ERROR_CANCELLED) {
        return ECANCELED;
    }
    if (err->code == G_IO_ERROR_NOT_FOUND) {
        return ENOENT;
    }
    if (err->code == G_IO_ERROR_EXISTS || err->code == G_IO_ERROR_WOULD_MERGE) {
        return EEXIST;
    }
    if (err->code == G_IO_ERROR_IS_DIRECTORY) {
        return EISDIR;
    }
    if (err->code == G_IO_ERROR_NOT_DIRECTORY) {
        return ENOTDIR;
    }
    if (err->code == G_IO_ERROR_NOT_EMPTY) {
        return ENOTEMPTY;
    }
    if (err->code == G_IO_ERROR_PERMISSION_DENIED || err->code == G_IO_ERROR_PROXY_AUTH_FAILED ||
        err->code == G_IO_ERROR_PROXY_NEED_AUTH || err->code == G_IO_ERROR_PROXY_NOT_ALLOWED) {
        return EACCES;
    }
    if (err->code == G_IO_ERROR_NO_SPACE) {
        return ENOSPC;
    }
    if (err->code == G_IO_ERROR_INVALID_ARGUMENT || err->code == G_IO_ERROR_INVALID_FILENAME ||
        err->code == G_IO_ERROR_NOT_REGULAR_FILE || err->code == G_IO_ERROR_NOT_SYMBOLIC_LINK ||
        err->code == G_IO_ERROR_INVALID_DATA || err->code == G_IO_ERROR_DESTINATION_UNSET) {
        return EINVAL;
    }
    if (err->code == G_IO_ERROR_NOT_SUPPORTED || err->code == G_IO_ERROR_NOT_MOUNTABLE_FILE) {
        return ENOTSUP;
    }
    if (err->code == G_IO_ERROR_READ_ONLY) {
        return EROFS;
    }
    if (err->code == G_IO_ERROR_WOULD_RECURSE) {
        return ELOOP;
    }
    if (err->code == G_IO_ERROR_BUSY || err->code == G_IO_ERROR_ALREADY_MOUNTED || err->code == G_IO_ERROR_PENDING) {
        return EBUSY;
    }
    if (err->code == G_IO_ERROR_WOULD_BLOCK) {
        return EAGAIN;
    }
    if (err->code == G_IO_ERROR_TIMED_OUT) {
        return ETIMEDOUT;
    }
    if (err->code == G_IO_ERROR_HOST_NOT_FOUND) {
        return ENOENT;
    }
    if (err->code == G_IO_ERROR_TOO_MANY_OPEN_FILES) {
        return EMFILE;
    }
    if (err->code == G_IO_ERROR_TOO_MANY_LINKS) {
        return EMLINK;
    }
    if (err->code == G_IO_ERROR_FILENAME_TOO_LONG) {
        return ENAMETOOLONG;
    }
    if (err->code == G_IO_ERROR_NOT_MOUNTED || err->code == G_IO_ERROR_NO_SUCH_DEVICE) {
        return ENODEV;
    }
    if (err->code == G_IO_ERROR_WRONG_ETAG) {
        return ESTALE;
    }
    if (err->code == G_IO_ERROR_CONNECTION_REFUSED) {
        return ECONNREFUSED;
    }
    if (err->code == G_IO_ERROR_NETWORK_UNREACHABLE) {
        return ENETUNREACH;
    }
    if (err->code == G_IO_ERROR_HOST_UNREACHABLE) {
        return EHOSTUNREACH;
    }
    if (err->code == G_IO_ERROR_CONNECTION_CLOSED) {
        return ECONNRESET;
    }
    if (err->code == G_IO_ERROR_BROKEN_PIPE) {
        return EPIPE;
    }
    if (err->code == G_IO_ERROR_NOT_CONNECTED) {
        return ENOTCONN;
    }
    if (err->code == G_IO_ERROR_ADDRESS_IN_USE) {
        return EADDRINUSE;
    }
    if (err->code == G_IO_ERROR_MESSAGE_TOO_LARGE) {
        return EMSGSIZE;
    }

    return EIO;
}

void set_gio_error(Error& out, OperationStep step, const GError* gioError, const char* context) {
    const int sysErrno = errno_from_gio_error(gioError);
    if (sysErrno == ECANCELED) {
        set_cancelled(out, step, gioError && gioError->message ? gioError->message : std::string());
        return;
    }

    std::string message(context ? context : "GIO operation failed");
    if (gioError && gioError->message && gioError->message[0] != '\0') {
        message += ": ";
        message += gioError->message;
    }

    set_error(out, EngineErrorCode::OperationFailed, step, sysErrno, message);
}

bool sync_cancellation(const Request& request, GCancellable* cancellable) {
    if (!is_cancel_requested(request)) {
        return false;
    }

    if (cancellable) {
        g_cancellable_cancel(cancellable);
    }
    return true;
}

bool query_entry_info(GFile* file,
                      const char* attrs,
                      GFileQueryInfoFlags flags,
                      GCancellable* cancellable,
                      GObjectPtr<GFileInfo>& outInfo,
                      Error& err,
                      OperationStep step,
                      const char* context) {
    outInfo.reset();

    GError* rawError = nullptr;
    GFileInfo* info = g_file_query_info(file, attrs, flags, cancellable, &rawError);
    GErrorPtr gioError(rawError);
    if (info) {
        outInfo.reset(info);
        return true;
    }

    set_gio_error(err, step, gioError.get(), context);
    return false;
}

bool scan_path_stats_recursive(const Request& request,
                               GFile* file,
                               GCancellable* cancellable,
                               SourceStats& stats,
                               Error& err) {
    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::BuildPlan);
        return false;
    }

    GObjectPtr<GFileInfo> info;
    if (!query_entry_info(file, "standard::type,standard::size", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, info,
                          err, OperationStep::BuildPlan, "g_file_query_info")) {
        return false;
    }

    add_int_saturated(stats.entryCount, 1);

    const GFileType type = g_file_info_get_file_type(info.get());
    if (type == G_FILE_TYPE_REGULAR) {
        const guint64 size = g_file_info_get_size(info.get());
        add_u64_saturated(stats.bytesTotal, static_cast<std::uint64_t>(size));
    }

    if (type != G_FILE_TYPE_DIRECTORY) {
        return true;
    }

    GError* enumerateErrorRaw = nullptr;
    GObjectPtr<GFileEnumerator> enumerator(g_file_enumerate_children(
        file, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &enumerateErrorRaw));
    GErrorPtr enumerateError(enumerateErrorRaw);
    if (!enumerator) {
        set_gio_error(err, OperationStep::BuildPlan, enumerateError.get(), "g_file_enumerate_children");
        return false;
    }

    for (;;) {
        if (sync_cancellation(request, cancellable)) {
            set_cancelled(err, OperationStep::BuildPlan);
            return false;
        }

        GError* nextErrorRaw = nullptr;
        GObjectPtr<GFileInfo> childInfo(g_file_enumerator_next_file(enumerator.get(), cancellable, &nextErrorRaw));
        GErrorPtr nextError(nextErrorRaw);

        if (!childInfo) {
            if (nextError) {
                set_gio_error(err, OperationStep::BuildPlan, nextError.get(), "g_file_enumerator_next_file");
                return false;
            }
            break;
        }

        const char* childName = g_file_info_get_name(childInfo.get());
        if (!childName || childName[0] == '\0') {
            continue;
        }

        GObjectPtr<GFile> child(g_file_get_child(file, childName));
        if (!child) {
            set_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, EIO,
                      "Unable to construct child endpoint during stats scan");
            return false;
        }

        if (!scan_path_stats_recursive(request, child.get(), cancellable, stats, err)) {
            return false;
        }
    }

    g_file_enumerator_close(enumerator.get(), cancellable, nullptr);
    return true;
}

bool destination_exists(const Request& request,
                        const std::string& endpoint,
                        EndpointKind kind,
                        GCancellable* cancellable,
                        bool& exists,
                        bool& isDirectory,
                        Error& err) {
    exists = false;
    isDirectory = false;

    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::ResolveConflict);
        return false;
    }

    GObjectPtr<GFile> file = make_gfile(endpoint, kind);
    if (!file) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ResolveConflict, EINVAL,
                  "Unable to create destination endpoint handle");
        return false;
    }

    GError* rawError = nullptr;
    GObjectPtr<GFileInfo> info(
        g_file_query_info(file.get(), "standard::type", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &rawError));
    GErrorPtr gioError(rawError);

    if (!info) {
        const int sysErrno = errno_from_gio_error(gioError.get());
        if (sysErrno == ENOENT) {
            exists = false;
            return true;
        }
        set_gio_error(err, OperationStep::ResolveConflict, gioError.get(), "g_file_query_info");
        return false;
    }

    exists = true;
    isDirectory = (g_file_info_get_file_type(info.get()) == G_FILE_TYPE_DIRECTORY);
    return true;
}

bool normalize_conflict_resolution(ConflictResolution input,
                                   ConflictResolution& normalized,
                                   bool& applyToAll,
                                   Error& err) {
    applyToAll = false;
    switch (input) {
        case ConflictResolution::Overwrite:
        case ConflictResolution::Skip:
        case ConflictResolution::Rename:
        case ConflictResolution::Abort:
            normalized = input;
            return true;
        case ConflictResolution::OverwriteAll:
            normalized = ConflictResolution::Overwrite;
            applyToAll = true;
            return true;
        case ConflictResolution::SkipAll:
            normalized = ConflictResolution::Skip;
            applyToAll = true;
            return true;
        case ConflictResolution::RenameAll:
            normalized = ConflictResolution::Rename;
            applyToAll = true;
            return true;
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EINVAL,
              "Unknown conflict resolution decision");
    return false;
}

bool choose_rename_destination(const Request& request,
                               const std::string& preferredDestination,
                               EndpointKind destinationKind,
                               GCancellable* cancellable,
                               std::string& renamedDestination,
                               Error& err) {
    GObjectPtr<GFile> destination = make_gfile(preferredDestination, destinationKind);
    if (!destination) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ResolveConflict, EINVAL,
                  "Conflict rename destination is invalid");
        return false;
    }

    GObjectPtr<GFile> parent(g_file_get_parent(destination.get()));
    char* rawName = g_file_get_basename(destination.get());
    if (!rawName) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ResolveConflict, EINVAL,
                  "Conflict rename destination is invalid");
        return false;
    }

    const std::string name(rawName);
    g_free(rawName);

    if (name.empty() || is_dot_or_dotdot(name)) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ResolveConflict, EINVAL,
                  "Conflict rename destination is invalid");
        return false;
    }

    std::string stem;
    std::string extension;
    split_stem_and_extension(name, stem, extension);

    constexpr int kMaxAttempts = 10000;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        const std::string suffix =
            (attempt == 1) ? std::string(" (copy)") : std::string(" (copy ") + std::to_string(attempt) + ")";
        const std::string candidateName = stem + suffix + extension;

        GObjectPtr<GFile> candidate;
        if (parent) {
            candidate.reset(g_file_get_child(parent.get(), candidateName.c_str()));
        }
        else {
            candidate = make_gfile(candidateName, destinationKind);
        }

        if (!candidate) {
            set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EIO,
                      "Unable to construct conflict rename candidate");
            return false;
        }

        const std::string candidateEndpoint = endpoint_from_gfile(candidate.get(), destinationKind);
        if (candidateEndpoint.empty()) {
            set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EIO,
                      "Unable to encode conflict rename candidate");
            return false;
        }

        bool exists = false;
        bool isDirectory = false;
        if (!destination_exists(request, candidateEndpoint, destinationKind, cancellable, exists, isDirectory, err)) {
            return false;
        }
        if (!exists) {
            renamedDestination = candidateEndpoint;
            return true;
        }
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EEXIST,
              "Unable to find a unique destination name");
    return false;
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
    next.phase = (state.phase == ProgressPhase::Finalizing || next.phase == ProgressPhase::Finalizing)
                     ? ProgressPhase::Finalizing
                     : ProgressPhase::Running;

    state = next;
    if (handlers.onProgress) {
        handlers.onProgress(state);
    }
}

void mark_plan_complete_without_execution(const SourcePlan& plan,
                                          const ProgressSnapshot& totals,
                                          int& completedUnits,
                                          std::uint64_t& completedBytes,
                                          ProgressSnapshot& lastProgress,
                                          const EventHandlers& handlers) {
    add_int_saturated(completedUnits, plan.workUnits);
    add_u64_saturated(completedBytes, plan.stats.bytesTotal);

    ProgressSnapshot skippedProgress;
    skippedProgress.filesTotal = totals.filesTotal;
    skippedProgress.bytesTotal = totals.bytesTotal;
    skippedProgress.filesDone = completedUnits;
    skippedProgress.bytesDone = completedBytes;
    skippedProgress.currentPath = plan.sourcePath;
    skippedProgress.phase = ProgressPhase::Running;
    emit_monotonic_progress(skippedProgress, lastProgress, handlers);
}

bool build_plan(const Request& request,
                GCancellable* cancellable,
                std::vector<SourcePlan>& plans,
                ProgressSnapshot& totals,
                Error& err) {
    plans.clear();
    totals = {};

    plans.reserve(request.sources.size());

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (sync_cancellation(request, cancellable)) {
            set_cancelled(err, OperationStep::BuildPlan);
            return false;
        }

        const std::string& source = request.sources[index];
        if (source.empty()) {
            set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                      "Source path must not be empty");
            return false;
        }

        SourcePlan plan;
        plan.sourcePath = source;
        plan.sourceKind = source_endpoint_kind(request, index);

        {
            GObjectPtr<GFile> sourceFile = make_gfile(plan.sourcePath, plan.sourceKind);
            if (!sourceFile) {
                set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                          "Unable to open source endpoint");
                return false;
            }

            if (!scan_path_stats_recursive(request, sourceFile.get(), cancellable, plan.stats, err)) {
                return false;
            }
        }

        plan.workUnits = (request.operation == Operation::Delete) ? std::max(1, plan.stats.entryCount) : 1;

        if (is_copy_like(request.operation)) {
            plan.destinationKind = request.routing.destinationKind;
            if (request.destination.mappingMode == DestinationMappingMode::ExplicitPerSource) {
                plan.destinationPath = request.destination.explicitTargets[index];
            }
            else {
                std::string basename;
                if (!endpoint_basename(plan.sourcePath, plan.sourceKind, basename, err)) {
                    return false;
                }

                if (!build_child_endpoint(request.destination.targetDir, request.routing.destinationKind, basename,
                                          plan.destinationPath, err)) {
                    return false;
                }
            }
        }
        else if (request.operation == Operation::Untrash) {
            GObjectPtr<GFile> sourceFile = make_gfile(plan.sourcePath, plan.sourceKind);
            if (!sourceFile) {
                set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                          "Unable to open source endpoint");
                return false;
            }

            GObjectPtr<GFileInfo> info;
            if (!query_entry_info(sourceFile.get(), "trash::orig-path", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                  cancellable, info, err, OperationStep::BuildPlan, "g_file_query_info")) {
                return false;
            }

            const char* origPath = g_file_info_get_attribute_byte_string(info.get(), "trash::orig-path");
            if (!origPath || origPath[0] == '\0') {
                set_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, EINVAL,
                          "Cannot untrash source: trash::orig-path is unavailable");
                return false;
            }

            plan.destinationPath = origPath;
            plan.destinationKind = infer_endpoint_kind(plan.destinationPath);
        }

        add_int_saturated(totals.filesTotal, plan.workUnits);
        add_u64_saturated(totals.bytesTotal, plan.stats.bytesTotal);

        plans.push_back(std::move(plan));
    }

    return true;
}

bool resolve_conflict_for_plan(const Request& request,
                               const EventHandlers& handlers,
                               const SourcePlan& plan,
                               const std::string& preferredDestination,
                               std::string& resolvedDestination,
                               ConflictResolutionState& resolutionState,
                               bool& skip,
                               bool& allowOverwrite,
                               GCancellable* cancellable,
                               Error& err) {
    skip = false;
    allowOverwrite = true;

    if (!operation_uses_destination(request.operation)) {
        return true;
    }

    bool exists = false;
    bool isDirectory = false;
    if (!destination_exists(request, resolvedDestination, plan.destinationKind, cancellable, exists, isDirectory,
                            err)) {
        return false;
    }

    if (!exists) {
        switch (request.conflictPolicy) {
            case ConflictPolicy::Overwrite:
                allowOverwrite = true;
                return true;
            case ConflictPolicy::Skip:
            case ConflictPolicy::Rename:
                allowOverwrite = false;
                return true;
            case ConflictPolicy::Prompt:
                if (!resolutionState.hasApplyToAllResolution) {
                    allowOverwrite = false;
                    return true;
                }

                switch (resolutionState.applyToAllResolution) {
                    case ConflictResolution::Overwrite:
                        allowOverwrite = true;
                        return true;
                    case ConflictResolution::Skip:
                    case ConflictResolution::Rename:
                        allowOverwrite = false;
                        return true;
                    case ConflictResolution::Abort:
                        set_error(err, EngineErrorCode::ConflictAborted, OperationStep::ResolveConflict, ECANCELED,
                                  "Operation aborted during conflict resolution");
                        return false;
                    case ConflictResolution::OverwriteAll:
                    case ConflictResolution::SkipAll:
                    case ConflictResolution::RenameAll:
                        break;
                }
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
        case ConflictPolicy::Rename:
            resolution = ConflictResolution::Rename;
            break;
        case ConflictPolicy::Prompt:
            if (resolutionState.hasApplyToAllResolution) {
                resolution = resolutionState.applyToAllResolution;
                break;
            }

            if (!handlers.onConflict) {
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                          "Prompt conflict policy requires an onConflict handler");
                return false;
            }

            {
                const ConflictResolution rawResolution = handlers.onConflict(ConflictEvent{
                    ConflictKind::DestinationExists,
                    plan.sourcePath,
                    resolvedDestination,
                    isDirectory,
                });
                bool applyToAll = false;
                if (!normalize_conflict_resolution(rawResolution, resolution, applyToAll, err)) {
                    return false;
                }
                if (applyToAll) {
                    resolutionState.hasApplyToAllResolution = true;
                    resolutionState.applyToAllResolution = resolution;
                }
            }
            break;
    }

    switch (resolution) {
        case ConflictResolution::Overwrite:
            skip = false;
            allowOverwrite = true;
            return true;
        case ConflictResolution::Skip:
            skip = true;
            allowOverwrite = false;
            return true;
        case ConflictResolution::Abort:
            set_error(err, EngineErrorCode::ConflictAborted, OperationStep::ResolveConflict, ECANCELED,
                      "Operation aborted during conflict resolution");
            return false;
        case ConflictResolution::Rename:
            if (!choose_rename_destination(request, preferredDestination, plan.destinationKind, cancellable,
                                           resolvedDestination, err)) {
                return false;
            }
            skip = false;
            allowOverwrite = false;
            return true;
        case ConflictResolution::OverwriteAll:
        case ConflictResolution::SkipAll:
        case ConflictResolution::RenameAll:
            break;
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EINVAL,
              "Unknown conflict resolution decision");
    return false;
}

bool ensure_destination_directory(const Request& request,
                                  GFile* destination,
                                  bool overwrite,
                                  GCancellable* cancellable,
                                  Error& err) {
    GError* infoErrorRaw = nullptr;
    GObjectPtr<GFileInfo> existingInfo(g_file_query_info(
        destination, "standard::type", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &infoErrorRaw));
    GErrorPtr infoError(infoErrorRaw);

    if (existingInfo) {
        if (g_file_info_get_file_type(existingInfo.get()) == G_FILE_TYPE_DIRECTORY) {
            return true;
        }

        if (!overwrite) {
            set_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, EEXIST,
                      "Destination exists and overwrite is disabled");
            return false;
        }

        GError* deleteErrorRaw = nullptr;
        const gboolean removed = g_file_delete(destination, cancellable, &deleteErrorRaw);
        GErrorPtr deleteError(deleteErrorRaw);
        if (!removed) {
            set_gio_error(err, OperationStep::Execute, deleteError.get(), "g_file_delete");
            return false;
        }
    }
    else {
        const int sysErrno = errno_from_gio_error(infoError.get());
        if (sysErrno != ENOENT) {
            set_gio_error(err, OperationStep::Execute, infoError.get(), "g_file_query_info");
            return false;
        }
    }

    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    GError* mkdirErrorRaw = nullptr;
    const gboolean created = g_file_make_directory(destination, cancellable, &mkdirErrorRaw);
    GErrorPtr mkdirError(mkdirErrorRaw);
    if (created) {
        return true;
    }

    const int mkdirErrno = errno_from_gio_error(mkdirError.get());
    if (mkdirErrno == EEXIST) {
        GError* checkErrorRaw = nullptr;
        GObjectPtr<GFileInfo> checkInfo(g_file_query_info(
            destination, "standard::type", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &checkErrorRaw));
        GErrorPtr checkError(checkErrorRaw);
        if (checkInfo && g_file_info_get_file_type(checkInfo.get()) == G_FILE_TYPE_DIRECTORY) {
            return true;
        }
        if (checkError) {
            set_gio_error(err, OperationStep::Execute, checkError.get(), "g_file_query_info");
            return false;
        }
    }

    set_gio_error(err, OperationStep::Execute, mkdirError.get(), "g_file_make_directory");
    return false;
}

bool copy_entry_recursive(const Request& request,
                          GFile* source,
                          EndpointKind sourceKind,
                          GFile* destination,
                          EndpointKind destinationKind,
                          bool overwrite,
                          GCancellable* cancellable,
                          std::uint64_t& localBytesDone,
                          const PerSourceProgressCallback& callback,
                          Error& err) {
    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    const std::string sourceEndpoint = endpoint_from_gfile(source, sourceKind);

    GObjectPtr<GFileInfo> sourceInfo;
    if (!query_entry_info(source, "standard::type,standard::name,standard::size", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                          cancellable, sourceInfo, err, OperationStep::Execute, "g_file_query_info")) {
        return false;
    }

    const GFileType type = g_file_info_get_file_type(sourceInfo.get());
    if (type == G_FILE_TYPE_DIRECTORY) {
        if (!ensure_destination_directory(request, destination, overwrite, cancellable, err)) {
            return false;
        }

        GError* enumerateErrorRaw = nullptr;
        GObjectPtr<GFileEnumerator> enumerator(g_file_enumerate_children(
            source, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &enumerateErrorRaw));
        GErrorPtr enumerateError(enumerateErrorRaw);
        if (!enumerator) {
            set_gio_error(err, OperationStep::Execute, enumerateError.get(), "g_file_enumerate_children");
            return false;
        }

        for (;;) {
            if (sync_cancellation(request, cancellable)) {
                set_cancelled(err, OperationStep::Execute);
                return false;
            }

            GError* nextErrorRaw = nullptr;
            GObjectPtr<GFileInfo> childInfo(g_file_enumerator_next_file(enumerator.get(), cancellable, &nextErrorRaw));
            GErrorPtr nextError(nextErrorRaw);
            if (!childInfo) {
                if (nextError) {
                    set_gio_error(err, OperationStep::Execute, nextError.get(), "g_file_enumerator_next_file");
                    return false;
                }
                break;
            }

            const char* childName = g_file_info_get_name(childInfo.get());
            if (!childName || childName[0] == '\0') {
                continue;
            }

            GObjectPtr<GFile> childSource(g_file_get_child(source, childName));
            GObjectPtr<GFile> childDestination(g_file_get_child(destination, childName));
            if (!childSource || !childDestination) {
                set_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, EIO,
                          "Unable to construct child endpoint during copy");
                return false;
            }

            if (!copy_entry_recursive(request, childSource.get(), sourceKind, childDestination.get(), destinationKind,
                                      overwrite, cancellable, localBytesDone, callback, err)) {
                return false;
            }
        }

        g_file_enumerator_close(enumerator.get(), cancellable, nullptr);
        return true;
    }

    const std::uint64_t baseBytes = localBytesDone;
    const std::uint64_t sourceSize = static_cast<std::uint64_t>(g_file_info_get_size(sourceInfo.get()));

    ByteProgressBridge progressBridge;
    progressBridge.callback = &callback;
    progressBridge.baseBytes = baseBytes;
    progressBridge.currentPath = sourceEndpoint;
    progressBridge.cancellable = cancellable;

    GFileCopyFlags flags = static_cast<GFileCopyFlags>(G_FILE_COPY_ALL_METADATA | G_FILE_COPY_NOFOLLOW_SYMLINKS);
    if (overwrite) {
        flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
    }

    GError* copyErrorRaw = nullptr;
    const gboolean copied =
        g_file_copy(source, destination, flags, cancellable, gio_byte_progress, &progressBridge, &copyErrorRaw);
    GErrorPtr copyError(copyErrorRaw);
    if (!copied) {
        set_gio_error(err, OperationStep::Execute, copyError.get(), "g_file_copy");
        return false;
    }

    localBytesDone = baseBytes + sourceSize;
    if (!callback(0, localBytesDone, sourceEndpoint)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    return true;
}

bool delete_entry_recursive(const Request& request,
                            GFile* source,
                            EndpointKind sourceKind,
                            const std::string& sourceEndpoint,
                            GCancellable* cancellable,
                            bool deleteSelf,
                            bool reportProgress,
                            int& localFilesDone,
                            std::uint64_t& localBytesDone,
                            const PerSourceProgressCallback& callback,
                            Error& err) {
    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    GObjectPtr<GFileInfo> sourceInfo;
    if (!query_entry_info(source, "standard::type,standard::name,standard::size", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                          cancellable, sourceInfo, err, OperationStep::Execute, "g_file_query_info")) {
        return false;
    }

    const GFileType type = g_file_info_get_file_type(sourceInfo.get());
    if (type == G_FILE_TYPE_DIRECTORY) {
        GError* enumerateErrorRaw = nullptr;
        GObjectPtr<GFileEnumerator> enumerator(g_file_enumerate_children(
            source, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, &enumerateErrorRaw));
        GErrorPtr enumerateError(enumerateErrorRaw);
        if (!enumerator) {
            set_gio_error(err, OperationStep::Execute, enumerateError.get(), "g_file_enumerate_children");
            return false;
        }

        for (;;) {
            if (sync_cancellation(request, cancellable)) {
                set_cancelled(err, OperationStep::Execute);
                return false;
            }

            GError* nextErrorRaw = nullptr;
            GObjectPtr<GFileInfo> childInfo(g_file_enumerator_next_file(enumerator.get(), cancellable, &nextErrorRaw));
            GErrorPtr nextError(nextErrorRaw);
            if (!childInfo) {
                if (nextError) {
                    set_gio_error(err, OperationStep::Execute, nextError.get(), "g_file_enumerator_next_file");
                    return false;
                }
                break;
            }

            const char* childName = g_file_info_get_name(childInfo.get());
            if (!childName || childName[0] == '\0') {
                continue;
            }

            GObjectPtr<GFile> childSource(g_file_get_child(source, childName));
            if (!childSource) {
                set_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, EIO,
                          "Unable to construct child endpoint during delete");
                return false;
            }

            const std::string childEndpoint = endpoint_from_gfile(childSource.get(), sourceKind);
            if (!delete_entry_recursive(request, childSource.get(), sourceKind, childEndpoint, cancellable,
                                        /*deleteSelf=*/true, reportProgress, localFilesDone, localBytesDone, callback,
                                        err)) {
                return false;
            }
        }

        g_file_enumerator_close(enumerator.get(), cancellable, nullptr);
    }

    if (!deleteSelf) {
        return true;
    }

    GError* deleteErrorRaw = nullptr;
    const gboolean removed = g_file_delete(source, cancellable, &deleteErrorRaw);
    GErrorPtr deleteError(deleteErrorRaw);
    if (!removed) {
        set_gio_error(err, OperationStep::Execute, deleteError.get(), "g_file_delete");
        return false;
    }

    add_int_saturated(localFilesDone, 1);
    const GFileType infoType = g_file_info_get_file_type(sourceInfo.get());
    if (infoType == G_FILE_TYPE_REGULAR) {
        add_u64_saturated(localBytesDone, static_cast<std::uint64_t>(g_file_info_get_size(sourceInfo.get())));
    }

    if (reportProgress && !callback(localFilesDone, localBytesDone, sourceEndpoint)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    return true;
}

bool is_move_fallback_error(const GError* err) {
    if (!err || err->domain != G_IO_ERROR) {
        return false;
    }

    return err->code == G_IO_ERROR_WOULD_RECURSE || err->code == G_IO_ERROR_NOT_SUPPORTED;
}

bool execute_copy_operation(const Request& request,
                            const SourcePlan& plan,
                            const std::string& destinationPath,
                            bool overwrite,
                            GCancellable* cancellable,
                            std::uint64_t& localBytesDone,
                            const PerSourceProgressCallback& callback,
                            Error& err) {
    GObjectPtr<GFile> source = make_gfile(plan.sourcePath, plan.sourceKind);
    GObjectPtr<GFile> destination = make_gfile(destinationPath, plan.destinationKind);
    if (!source || !destination) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::Execute, EINVAL,
                  "Unable to construct source or destination endpoint");
        return false;
    }

    return copy_entry_recursive(request, source.get(), plan.sourceKind, destination.get(), plan.destinationKind,
                                overwrite, cancellable, localBytesDone, callback, err);
}

bool execute_move_operation(const Request& request,
                            const SourcePlan& plan,
                            const std::string& destinationPath,
                            bool overwrite,
                            GCancellable* cancellable,
                            std::uint64_t& localBytesDone,
                            const PerSourceProgressCallback& callback,
                            Error& err) {
    GObjectPtr<GFile> source = make_gfile(plan.sourcePath, plan.sourceKind);
    GObjectPtr<GFile> destination = make_gfile(destinationPath, plan.destinationKind);
    if (!source || !destination) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::Execute, EINVAL,
                  "Unable to construct source or destination endpoint");
        return false;
    }

    const std::uint64_t baseBytes = localBytesDone;

    ByteProgressBridge progressBridge;
    progressBridge.callback = &callback;
    progressBridge.baseBytes = baseBytes;
    progressBridge.currentPath = plan.sourcePath;
    progressBridge.cancellable = cancellable;

    GFileCopyFlags flags = static_cast<GFileCopyFlags>(G_FILE_COPY_ALL_METADATA | G_FILE_COPY_NOFOLLOW_SYMLINKS);
    if (overwrite) {
        flags = static_cast<GFileCopyFlags>(flags | G_FILE_COPY_OVERWRITE);
    }

    GError* moveErrorRaw = nullptr;
    const gboolean moved = g_file_move(source.get(), destination.get(), flags, cancellable, gio_byte_progress,
                                       &progressBridge, &moveErrorRaw);
    GErrorPtr moveError(moveErrorRaw);
    if (moved) {
        localBytesDone = baseBytes + plan.stats.bytesTotal;
        if (!callback(0, localBytesDone, plan.sourcePath)) {
            set_cancelled(err, OperationStep::Execute);
            return false;
        }
        return true;
    }

    if (!is_move_fallback_error(moveError.get())) {
        set_gio_error(err, OperationStep::Execute, moveError.get(), "g_file_move");
        return false;
    }

    moveError.reset();

    if (!copy_entry_recursive(request, source.get(), plan.sourceKind, destination.get(), plan.destinationKind,
                              overwrite, cancellable, localBytesDone, callback, err)) {
        return false;
    }

    int deleteUnits = 0;
    std::uint64_t deleteBytes = 0;
    if (!delete_entry_recursive(request, source.get(), plan.sourceKind, plan.sourcePath, cancellable,
                                /*deleteSelf=*/true, /*reportProgress=*/false, deleteUnits, deleteBytes, callback,
                                err)) {
        return false;
    }

    return true;
}

bool execute_delete_operation(const Request& request,
                              const SourcePlan& plan,
                              GCancellable* cancellable,
                              int& localFilesDone,
                              std::uint64_t& localBytesDone,
                              const PerSourceProgressCallback& callback,
                              Error& err) {
    GObjectPtr<GFile> source = make_gfile(plan.sourcePath, plan.sourceKind);
    if (!source) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::Execute, EINVAL,
                  "Unable to construct source endpoint");
        return false;
    }

    const bool deleteSelf = !(plan.sourceKind == EndpointKind::Uri && plan.sourcePath == "trash:///");
    return delete_entry_recursive(request, source.get(), plan.sourceKind, plan.sourcePath, cancellable, deleteSelf,
                                  /*reportProgress=*/true, localFilesDone, localBytesDone, callback, err);
}

bool execute_trash_operation(const Request& request,
                             const SourcePlan& plan,
                             GCancellable* cancellable,
                             int& localFilesDone,
                             std::uint64_t& localBytesDone,
                             const PerSourceProgressCallback& callback,
                             Error& err) {
    if (sync_cancellation(request, cancellable)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    GObjectPtr<GFile> source = make_gfile(plan.sourcePath, plan.sourceKind);
    if (!source) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::Execute, EINVAL,
                  "Unable to construct source endpoint");
        return false;
    }

    GError* trashErrorRaw = nullptr;
    const gboolean trashed = g_file_trash(source.get(), cancellable, &trashErrorRaw);
    GErrorPtr trashError(trashErrorRaw);
    if (!trashed) {
        set_gio_error(err, OperationStep::Execute, trashError.get(), "g_file_trash");
        return false;
    }

    localFilesDone = 1;
    localBytesDone = plan.stats.bytesTotal;
    if (!callback(localFilesDone, localBytesDone, plan.sourcePath)) {
        set_cancelled(err, OperationStep::Execute);
        return false;
    }

    return true;
}

bool execute_plan_operation(const Request& request,
                            const SourcePlan& plan,
                            const std::string& destinationPath,
                            bool overwrite,
                            GCancellable* cancellable,
                            int& localFilesDone,
                            std::uint64_t& localBytesDone,
                            const PerSourceProgressCallback& callback,
                            Error& err) {
    localFilesDone = 0;
    localBytesDone = 0;

    switch (request.operation) {
        case Operation::Copy: {
            if (!execute_copy_operation(request, plan, destinationPath, overwrite, cancellable, localBytesDone,
                                        callback, err)) {
                return false;
            }
            localFilesDone = 1;
            return true;
        }
        case Operation::Move:
        case Operation::Untrash: {
            if (!execute_move_operation(request, plan, destinationPath, overwrite, cancellable, localBytesDone,
                                        callback, err)) {
                return false;
            }
            localFilesDone = 1;
            return true;
        }
        case Operation::Delete:
            return execute_delete_operation(request, plan, cancellable, localFilesDone, localBytesDone, callback, err);
        case Operation::Trash:
            return execute_trash_operation(request, plan, cancellable, localFilesDone, localBytesDone, callback, err);
        case Operation::Mkdir:
        case Operation::Link:
            set_error(err, EngineErrorCode::UnsupportedOperation, OperationStep::Execute, ENOTSUP,
                      "Operation is not supported by GIO executor");
            return false;
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, EINVAL, "Unknown operation");
    return false;
}

}  // namespace

Result run_gio_request(const Request& request, const EventHandlers& handlers) {
    Result result;

    if (is_cancel_requested(request)) {
        result.cancelled = true;
        set_cancelled(result.error, OperationStep::ValidateRequest);
        return result;
    }

    GObjectPtr<GCancellable> cancellable(g_cancellable_new());

    std::vector<SourcePlan> plans;
    ProgressSnapshot totals;
    if (!build_plan(request, cancellable.get(), plans, totals, result.error)) {
        result.cancelled = (result.error.sysErrno == ECANCELED);
        return result;
    }

    ProgressSnapshot lastProgress;
    lastProgress.filesTotal = totals.filesTotal;
    lastProgress.bytesTotal = totals.bytesTotal;

    int completedUnits = 0;
    std::uint64_t completedBytes = 0;
    ConflictResolutionState conflictState;

    for (const SourcePlan& plan : plans) {
        if (sync_cancellation(request, cancellable.get())) {
            result.cancelled = true;
            set_cancelled(result.error, OperationStep::Execute);
            result.finalProgress = lastProgress;
            return result;
        }

        std::string executionDestination = plan.destinationPath;
        bool skip = false;
        bool allowOverwrite = true;

        Error conflictError;
        if (!resolve_conflict_for_plan(request, handlers, plan, plan.destinationPath, executionDestination,
                                       conflictState, skip, allowOverwrite, cancellable.get(), conflictError)) {
            if (conflictError.code == EngineErrorCode::ConflictAborted || conflictError.sysErrno == ECANCELED) {
                result.cancelled = true;
                set_cancelled(
                    result.error, OperationStep::ResolveConflict,
                    conflictError.message.empty() ? std::string("Operation cancelled") : conflictError.message);
            }
            else {
                result.error = conflictError;
            }
            result.finalProgress = lastProgress;
            return result;
        }

        if (skip) {
            mark_plan_complete_without_execution(plan, totals, completedUnits, completedBytes, lastProgress, handlers);
            continue;
        }

        auto emitLocalProgress = [&request, &plan, &totals, &completedUnits, &completedBytes, &lastProgress, &handlers,
                                  &cancellable](int localFilesDone, std::uint64_t localBytesDone,
                                                const std::string& currentPath) {
            if (sync_cancellation(request, cancellable.get())) {
                return false;
            }

            const int clampedLocalFiles = std::clamp(localFilesDone, 0, plan.workUnits);
            const std::uint64_t clampedLocalBytes = std::min(localBytesDone, plan.stats.bytesTotal);

            ProgressSnapshot aggregate;
            aggregate.filesTotal = totals.filesTotal;
            aggregate.bytesTotal = totals.bytesTotal;
            aggregate.filesDone = sum_int_saturated(completedUnits, clampedLocalFiles);
            aggregate.bytesDone = sum_u64_saturated(completedBytes, clampedLocalBytes);
            aggregate.currentPath = currentPath.empty() ? plan.sourcePath : currentPath;
            aggregate.phase = ProgressPhase::Running;
            emit_monotonic_progress(aggregate, lastProgress, handlers);
            return !sync_cancellation(request, cancellable.get());
        };

        int localFilesDone = 0;
        std::uint64_t localBytesDone = 0;
        Error opError;
        bool executed = execute_plan_operation(request, plan, executionDestination, allowOverwrite, cancellable.get(),
                                               localFilesDone, localBytesDone, emitLocalProgress, opError);

        if (!executed && operation_uses_destination(request.operation) && opError.sysErrno == EEXIST &&
            !allowOverwrite) {
            bool lateSkip = false;
            bool lateAllowOverwrite = false;
            const std::string attemptedDestination = executionDestination;
            Error lateConflictError;
            if (!resolve_conflict_for_plan(request, handlers, plan, plan.destinationPath, executionDestination,
                                           conflictState, lateSkip, lateAllowOverwrite, cancellable.get(),
                                           lateConflictError)) {
                if (lateConflictError.code == EngineErrorCode::ConflictAborted ||
                    lateConflictError.sysErrno == ECANCELED) {
                    result.cancelled = true;
                    set_cancelled(result.error, OperationStep::ResolveConflict,
                                  lateConflictError.message.empty() ? std::string("Operation cancelled")
                                                                    : lateConflictError.message);
                }
                else {
                    result.error = lateConflictError;
                }
                result.finalProgress = lastProgress;
                return result;
            }

            if (lateSkip) {
                mark_plan_complete_without_execution(plan, totals, completedUnits, completedBytes, lastProgress,
                                                     handlers);
                continue;
            }

            if (lateAllowOverwrite || executionDestination != attemptedDestination) {
                localFilesDone = 0;
                localBytesDone = 0;
                opError = {};
                executed =
                    execute_plan_operation(request, plan, executionDestination, lateAllowOverwrite, cancellable.get(),
                                           localFilesDone, localBytesDone, emitLocalProgress, opError);
            }
        }

        if (!executed) {
            if (opError.sysErrno == ECANCELED || sync_cancellation(request, cancellable.get())) {
                result.cancelled = true;
                set_cancelled(result.error, OperationStep::Execute,
                              opError.message.empty() ? std::string("Operation cancelled") : opError.message);
            }
            else {
                result.error = opError;
            }
            result.finalProgress = lastProgress;
            return result;
        }

        add_int_saturated(completedUnits, plan.workUnits);
        add_u64_saturated(completedBytes, plan.stats.bytesTotal);

        ProgressSnapshot doneProgress;
        doneProgress.filesTotal = totals.filesTotal;
        doneProgress.bytesTotal = totals.bytesTotal;
        doneProgress.filesDone = completedUnits;
        doneProgress.bytesDone = completedBytes;
        doneProgress.currentPath = plan.sourcePath;
        doneProgress.phase = ProgressPhase::Running;
        emit_monotonic_progress(doneProgress, lastProgress, handlers);
    }

    ProgressSnapshot finalizingProgress;
    finalizingProgress.filesTotal = totals.filesTotal;
    finalizingProgress.bytesTotal = totals.bytesTotal;
    finalizingProgress.filesDone = completedUnits;
    finalizingProgress.bytesDone = completedBytes;
    finalizingProgress.currentPath = lastProgress.currentPath;
    finalizingProgress.phase = ProgressPhase::Finalizing;
    emit_monotonic_progress(finalizingProgress, lastProgress, handlers);

    result.success = true;
    result.finalProgress = lastProgress;
    return result;
}

}  // namespace Oneg4FM::FileOpsContract::detail
