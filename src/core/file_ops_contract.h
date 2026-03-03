/*
 * Unified file-operations contract (Linux-aware)
 * src/core/file_ops_contract.h
 */

#ifndef PCMANFM_FILE_OPS_CONTRACT_H
#define PCMANFM_FILE_OPS_CONTRACT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Oneg4FM::FileOpsContract {

enum class Operation {
    Copy,
    Move,
    Delete,
    Trash,
    Untrash,
    Mkdir,
    Link,
};

enum class DestinationMappingMode {
    SourceBasename,
    ExplicitPerSource,
};

struct DestinationPolicy {
    std::string targetDir;
    DestinationMappingMode mappingMode = DestinationMappingMode::SourceBasename;
    std::vector<std::string> explicitTargets;
};

enum class ConflictPolicy {
    Overwrite,
    Rename,
    Skip,
    Prompt,
};

enum class SymlinkCopyMode {
    CopyLinkAsLink,
    CopyTarget,
};

struct SymlinkPolicy {
    bool followSymlinks = false;
    SymlinkCopyMode copyMode = SymlinkCopyMode::CopyLinkAsLink;
};

struct MetadataPolicy {
    bool preserveOwnership = false;
    bool preservePermissions = true;
    bool preserveTimestamps = true;
};

struct AtomicityHints {
    bool requireAtomicReplace = false;
    bool bestEffortAtomicMove = true;
};

enum class CancelCheckpointGranularity {
    PerEntry,
    PerChunk,
};

enum class WorkerMode {
    InProcess,
    SandboxedThread,
};

struct LinuxSafetyRequirements {
    bool requireOpenat2Resolve = true;
    bool requireLandlock = false;
    bool requireSeccomp = false;
    WorkerMode workerMode = WorkerMode::InProcess;
};

enum class EndpointKind {
    NativePath,
    Uri,
};

enum class Backend {
    Auto,
    LocalHardened,
    Gio,
};

struct RoutingHints {
    Backend defaultBackend = Backend::Auto;
    std::vector<Backend> sourceBackends;
    Backend destinationBackend = Backend::Auto;
    std::vector<EndpointKind> sourceKinds;
    EndpointKind destinationKind = EndpointKind::NativePath;
};

struct Request {
    Operation operation = Operation::Copy;
    std::vector<std::string> sources;
    DestinationPolicy destination;
    ConflictPolicy conflictPolicy = ConflictPolicy::Overwrite;
    SymlinkPolicy symlinkPolicy;
    MetadataPolicy metadata;
    AtomicityHints atomicity;
    CancelCheckpointGranularity cancelGranularity = CancelCheckpointGranularity::PerChunk;
    std::function<bool()> cancellationRequested;
    LinuxSafetyRequirements linuxSafety;
    RoutingHints routing;
};

enum class ProgressPhase {
    Running,
    Finalizing,
};

struct ProgressSnapshot {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    int filesDone = 0;
    int filesTotal = 0;
    std::string currentPath;
    ProgressPhase phase = ProgressPhase::Running;
};

enum class ConflictKind {
    DestinationExists,
};

struct ConflictEvent {
    ConflictKind kind = ConflictKind::DestinationExists;
    std::string sourcePath;
    std::string destinationPath;
    bool destinationIsDirectory = false;
};

enum class ConflictResolution {
    Overwrite,
    Skip,
    Rename,
    Abort,
    OverwriteAll,
    SkipAll,
    RenameAll,
};

enum class EngineErrorCode {
    None,
    InvalidRequest,
    UnsupportedOperation,
    UnsupportedPolicy,
    UnsupportedFeature,
    SafetyRequirementUnavailable,
    ConflictAborted,
    OperationFailed,
    Cancelled,
};

enum class OperationStep {
    None,
    ValidateRequest,
    BuildPlan,
    ResolveConflict,
    Execute,
    Finalize,
};

struct Error {
    EngineErrorCode code = EngineErrorCode::None;
    OperationStep step = OperationStep::None;
    int sysErrno = 0;
    std::string message;

    bool isSet() const {
        return code != EngineErrorCode::None || step != OperationStep::None || sysErrno != 0 || !message.empty();
    }
};

struct Result {
    bool success = false;
    bool cancelled = false;
    ProgressSnapshot finalProgress;
    Error error;
};

using ProgressCallback = std::function<void(const ProgressSnapshot&)>;
using ConflictCallback = std::function<ConflictResolution(const ConflictEvent&)>;

struct EventHandlers {
    ProgressCallback onProgress;
    ConflictCallback onConflict;
};

struct BackendCapabilities {
    Backend backend = Backend::LocalHardened;
    bool available = false;
    bool supportsNativePaths = false;
    bool supportsUriPaths = false;
    bool supportsCopy = false;
    bool supportsMove = false;
    bool supportsDelete = false;
    bool supportsTrash = false;
    bool supportsUntrash = false;
    std::string unavailableReason;
};

struct CapabilityReport {
    BackendCapabilities localHardened;
    BackendCapabilities gio;
};

CapabilityReport capabilities();
Result preflight(const Request& request);
Result run(const Request& request, const EventHandlers& handlers = {});

}  // namespace Oneg4FM::FileOpsContract

#endif  // PCMANFM_FILE_OPS_CONTRACT_H
