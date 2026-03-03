/*
 * Unified file-operations contract (Linux-aware)
 * src/core/file_ops_contract.h
 */

#ifndef PCMANFM_FILE_OPS_CONTRACT_H
#define PCMANFM_FILE_OPS_CONTRACT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

struct SourceSnapshot {
    bool available = false;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::uint64_t size = 0;
    std::int64_t mtimeSec = 0;
    std::int64_t mtimeNsec = 0;
};

class CancelHandle {
   public:
    CancelHandle();

    void cancel() const noexcept;
    bool isCancelled() const noexcept;
    std::function<bool()> callback() const;

   private:
    struct State {
        std::atomic<bool> cancelled{false};
    };

    std::shared_ptr<State> state_;
};

struct Request {
    Operation operation = Operation::Copy;
    std::vector<std::string> sources;
    std::vector<SourceSnapshot> sourceSnapshots;
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

struct RequestOptions {
    SymlinkPolicy symlinkPolicy;
    MetadataPolicy metadata;
    AtomicityHints atomicity;
    CancelCheckpointGranularity cancelGranularity = CancelCheckpointGranularity::PerChunk;
    LinuxSafetyRequirements linuxSafety;
    RoutingHints routing;
};

struct RequestPolicy {
    ConflictPolicy conflictPolicy = ConflictPolicy::Overwrite;
};

struct UiContext {
    std::string initiator;
};

struct RequestCommon {
    std::string opId;
    std::vector<std::string> sources;
    std::vector<SourceSnapshot> sourceSnapshots;
    DestinationPolicy destination;
    RequestOptions options;
    RequestPolicy policy;
    UiContext uiContext;
    CancelHandle cancelHandle;
    std::function<bool()> cancellationRequested;
};

enum class TransferOperation {
    Copy,
    Move,
};

struct TransferRequest {
    TransferOperation transferOperation = TransferOperation::Copy;
    RequestCommon common;
};

struct DeleteRequest {
    RequestCommon common;
};

struct TrashRequest {
    RequestCommon common;
};

struct UntrashRequest {
    RequestCommon common;
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

struct ProgressEvent {
    ProgressSnapshot progress;
};

enum class PromptKind {
    ConflictResolution,
    Generic,
};

struct PromptEvent {
    PromptKind kind = PromptKind::Generic;
    std::string sourcePath;
    std::string destinationPath;
    std::string message;
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

struct ErrorEvent {
    Error error;
};

struct Result {
    bool success = false;
    bool cancelled = false;
    ProgressSnapshot finalProgress;
    Error error;
};

struct DoneEvent {
    Result result;
};

enum class OpStatus {
    Success,
    Cancelled,
    Failed,
};

struct OpCounters {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    int filesDone = 0;
    int filesTotal = 0;
};

struct OpDiagnostics {
    EngineErrorCode code = EngineErrorCode::None;
    OperationStep step = OperationStep::None;
    int sysErrno = 0;
    std::string message;
};

struct PerItemResult {
    std::string sourcePath;
    std::string destinationPath;
    OpStatus status = OpStatus::Failed;
    Error error;
};

struct OpResult {
    OpStatus status = OpStatus::Failed;
    std::vector<PerItemResult> perItemResults;
    OpDiagnostics diagnostics;
    OpCounters counters;
};

using ProgressCallback = std::function<void(const ProgressSnapshot&)>;
using ConflictCallback = std::function<ConflictResolution(const ConflictEvent&)>;

struct EventHandlers {
    ProgressCallback onProgress;
    ConflictCallback onConflict;
};

using ProgressEventCallback = std::function<void(const ProgressEvent&)>;
using PromptEventCallback = std::function<void(const PromptEvent&)>;
using ConflictEventCallback = std::function<ConflictResolution(const ConflictEvent&)>;
using DoneEventCallback = std::function<void(const DoneEvent&)>;
using ErrorEventCallback = std::function<void(const ErrorEvent&)>;

struct EventStreamHandlers {
    ProgressEventCallback onProgress;
    PromptEventCallback onPrompt;
    ConflictEventCallback onConflict;
    DoneEventCallback onDone;
    ErrorEventCallback onError;
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
Request toRequest(const TransferRequest& request);
Request toRequest(const DeleteRequest& request);
Request toRequest(const TrashRequest& request);
Request toRequest(const UntrashRequest& request);

OpResult toOpResult(const Result& result, const RequestCommon& common);

Result preflight(const Request& request);
Result preflight(const TransferRequest& request);
Result preflight(const DeleteRequest& request);
Result preflight(const TrashRequest& request);
Result preflight(const UntrashRequest& request);

Result run(const Request& request, const EventHandlers& handlers = {});
Result run(const Request& request, const EventHandlers& handlers, const EventStreamHandlers& streamHandlers);
Result run(const TransferRequest& request, const EventHandlers& handlers = {});
Result run(const DeleteRequest& request, const EventHandlers& handlers = {});
Result run(const TrashRequest& request, const EventHandlers& handlers = {});
Result run(const UntrashRequest& request, const EventHandlers& handlers = {});

OpResult runOp(const TransferRequest& request, const EventHandlers& handlers = {});
OpResult runOp(const DeleteRequest& request, const EventHandlers& handlers = {});
OpResult runOp(const TrashRequest& request, const EventHandlers& handlers = {});
OpResult runOp(const UntrashRequest& request, const EventHandlers& handlers = {});

}  // namespace Oneg4FM::FileOpsContract

#endif  // PCMANFM_FILE_OPS_CONTRACT_H
