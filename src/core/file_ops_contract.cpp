/*
 * Unified file-operations contract executor
 * src/core/file_ops_contract.cpp
 */

#include "file_ops_contract.h"

#include "gio_ops_executor.h"

#include "fs_ops.h"
#include "linux_fs_safety.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#if __has_include(<linux/landlock.h>)
#include <linux/landlock.h>
#define PCMANFM_HAVE_LANDLOCK_HEADER 1
#endif
#include <linux/seccomp.h>
#include <limits>
#include <limits.h>
#include <memory>
#include <mutex>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace Oneg4FM::FileOpsContract {
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

struct ConflictResolutionState {
    bool hasApplyToAllResolution = false;
    ConflictResolution applyToAllResolution = ConflictResolution::Overwrite;
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

bool is_dot_or_dotdot(const std::string& name) {
    return name == "." || name == "..";
}

bool split_parent_and_name(const std::string& path, std::string& parent, std::string& name) {
    const std::size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        parent.clear();
        name = path;
    }
    else if (pos == 0) {
        parent = "/";
        name = path.substr(1);
    }
    else {
        parent = path.substr(0, pos);
        name = path.substr(pos + 1);
    }

    if (name.empty() || is_dot_or_dotdot(name)) {
        return false;
    }
    return true;
}

bool canonicalize_path_with_missing_tail(const std::string& path, std::string& out, Error& err) {
    char* resolved = ::realpath(path.c_str(), nullptr);
    if (resolved != nullptr) {
        out.assign(resolved);
        ::free(resolved);
        return true;
    }

    const int errnum = errno;
    if (errnum != ENOENT) {
        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::BuildPlan, errnum, "realpath");
        return false;
    }

    std::string parent;
    std::string leaf;
    if (!split_parent_and_name(path, parent, leaf)) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Path canonicalization failed: invalid path");
        return false;
    }

    std::string canonical_parent;
    if (!canonicalize_path_with_missing_tail(parent, canonical_parent, err)) {
        return false;
    }

    out = join_path(canonical_parent, leaf);
    return true;
}

bool canonicalize_path_parent_for_local_execution(const std::string& path, std::string& out, Error& err) {
    std::string parent;
    std::string leaf;
    if (!split_parent_and_name(path, parent, leaf)) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::BuildPlan, EINVAL,
                  "Path canonicalization failed: invalid path");
        return false;
    }

    std::string canonical_parent;
    if (!canonicalize_path_with_missing_tail(parent, canonical_parent, err)) {
        return false;
    }

    out = join_path(canonical_parent, leaf);
    return true;
}

void split_stem_and_extension(const std::string& file_name, std::string& stem, std::string& extension) {
    const std::size_t dot = file_name.find_last_of('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < file_name.size()) {
        stem = file_name.substr(0, dot);
        extension = file_name.substr(dot);
        return;
    }

    stem = file_name;
    extension.clear();
}

bool normalize_conflict_resolution(ConflictResolution input,
                                   ConflictResolution& normalized,
                                   bool& apply_to_all,
                                   Error& err) {
    apply_to_all = false;
    switch (input) {
        case ConflictResolution::Overwrite:
        case ConflictResolution::Skip:
        case ConflictResolution::Rename:
        case ConflictResolution::Abort:
            normalized = input;
            return true;
        case ConflictResolution::OverwriteAll:
            normalized = ConflictResolution::Overwrite;
            apply_to_all = true;
            return true;
        case ConflictResolution::SkipAll:
            normalized = ConflictResolution::Skip;
            apply_to_all = true;
            return true;
        case ConflictResolution::RenameAll:
            normalized = ConflictResolution::Rename;
            apply_to_all = true;
            return true;
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EINVAL,
              "Unknown conflict resolution decision");
    return false;
}

const char* operation_name(Operation operation) {
    switch (operation) {
        case Operation::Copy:
            return "Copy";
        case Operation::Move:
            return "Move";
        case Operation::Delete:
            return "Delete";
        case Operation::Trash:
            return "Trash";
        case Operation::Untrash:
            return "Untrash";
        case Operation::Mkdir:
            return "Mkdir";
        case Operation::Link:
            return "Link";
    }
    return "Unknown";
}

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Auto:
            return "Auto";
        case Backend::LocalHardened:
            return "LocalHardened";
        case Backend::Gio:
            return "Gio";
    }
    return "Unknown";
}

bool backend_supports_operation(const BackendCapabilities& backend_capabilities, Operation operation) {
    switch (operation) {
        case Operation::Copy:
            return backend_capabilities.supportsCopy;
        case Operation::Move:
            return backend_capabilities.supportsMove;
        case Operation::Delete:
            return backend_capabilities.supportsDelete;
        case Operation::Trash:
            return backend_capabilities.supportsTrash;
        case Operation::Untrash:
            return backend_capabilities.supportsUntrash;
        case Operation::Mkdir:
        case Operation::Link:
            return false;
    }
    return false;
}

CapabilityReport make_capability_report() {
    CapabilityReport report;

    report.localHardened.backend = Backend::LocalHardened;
    report.localHardened.available = true;
    report.localHardened.supportsNativePaths = true;
    report.localHardened.supportsUriPaths = false;
    report.localHardened.supportsCopy = true;
    report.localHardened.supportsMove = true;
    report.localHardened.supportsDelete = true;
    report.localHardened.supportsTrash = false;
    report.localHardened.supportsUntrash = false;

    report.gio.backend = Backend::Gio;
    report.gio.available = true;
    report.gio.supportsNativePaths = true;
    report.gio.supportsUriPaths = true;
    report.gio.supportsCopy = true;
    report.gio.supportsMove = true;
    report.gio.supportsDelete = true;
    report.gio.supportsTrash = true;
    report.gio.supportsUntrash = true;

    return report;
}

const CapabilityReport& capability_report() {
    static const CapabilityReport report = make_capability_report();
    return report;
}

const BackendCapabilities* backend_capabilities_for(Backend backend) {
    const CapabilityReport& report = capability_report();
    switch (backend) {
        case Backend::LocalHardened:
            return &report.localHardened;
        case Backend::Gio:
            return &report.gio;
        case Backend::Auto:
            break;
    }
    return nullptr;
}

#ifndef SYS_seccomp
#ifdef __NR_seccomp
#define SYS_seccomp __NR_seccomp
#endif
#endif

#ifndef SYS_landlock_create_ruleset
#ifdef __NR_landlock_create_ruleset
#define SYS_landlock_create_ruleset __NR_landlock_create_ruleset
#endif
#endif

#ifndef SYS_landlock_add_rule
#ifdef __NR_landlock_add_rule
#define SYS_landlock_add_rule __NR_landlock_add_rule
#endif
#endif

#ifndef SYS_landlock_restrict_self
#ifdef __NR_landlock_restrict_self
#define SYS_landlock_restrict_self __NR_landlock_restrict_self
#endif
#endif

void set_linux_safety_error(Error& out, OperationStep step, int sys_errno, const std::string& message) {
    const EngineErrorCode code =
        (sys_errno == ENOSYS) ? EngineErrorCode::SafetyRequirementUnavailable : EngineErrorCode::OperationFailed;
    set_error(out, code, step, sys_errno, message);
}

bool set_no_new_privs(Error& err) {
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0) {
        return true;
    }
    set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno, "prctl(PR_SET_NO_NEW_PRIVS)");
    return false;
}

int seccomp_raw(unsigned int operation, unsigned int flags, void* args) {
#ifdef SYS_seccomp
    return static_cast<int>(::syscall(SYS_seccomp, operation, flags, args));
#else
    (void)operation;
    (void)flags;
    (void)args;
    errno = ENOSYS;
    return -1;
#endif
}

bool expected_seccomp_arch(std::uint32_t& out_arch) {
#if defined(__x86_64__)
    out_arch = AUDIT_ARCH_X86_64;
    return true;
#elif defined(__aarch64__)
    out_arch = AUDIT_ARCH_AARCH64;
    return true;
#elif defined(__i386__)
    out_arch = AUDIT_ARCH_I386;
    return true;
#elif defined(__arm__)
    out_arch = AUDIT_ARCH_ARM;
    return true;
#else
    return false;
#endif
}

void append_seccomp_deny_syscall(std::vector<sock_filter>& filter, int syscall_nr) {
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(syscall_nr), 0, 1));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<std::uint32_t>(EPERM)));
}

bool install_seccomp_filter(Error& err) {
    std::uint32_t arch = 0;
    if (!expected_seccomp_arch(arch)) {
        set_error(err, EngineErrorCode::UnsupportedFeature, OperationStep::Execute, ENOTSUP,
                  "Seccomp filter is not supported on this architecture");
        return false;
    }

    std::vector<sock_filter> filter;
    filter.reserve(40);

    filter.push_back(
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<std::uint32_t>(offsetof(struct seccomp_data, arch))));
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, arch, 1, 0));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | static_cast<std::uint32_t>(EPERM)));
    filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<std::uint32_t>(offsetof(struct seccomp_data, nr))));

#ifdef SYS_mount
    append_seccomp_deny_syscall(filter, SYS_mount);
#endif
#ifdef SYS_umount2
    append_seccomp_deny_syscall(filter, SYS_umount2);
#endif
#ifdef SYS_pivot_root
    append_seccomp_deny_syscall(filter, SYS_pivot_root);
#endif
#ifdef SYS_init_module
    append_seccomp_deny_syscall(filter, SYS_init_module);
#endif
#ifdef SYS_finit_module
    append_seccomp_deny_syscall(filter, SYS_finit_module);
#endif
#ifdef SYS_delete_module
    append_seccomp_deny_syscall(filter, SYS_delete_module);
#endif
#ifdef SYS_kexec_load
    append_seccomp_deny_syscall(filter, SYS_kexec_load);
#endif
#ifdef SYS_kexec_file_load
    append_seccomp_deny_syscall(filter, SYS_kexec_file_load);
#endif
#ifdef SYS_open_by_handle_at
    append_seccomp_deny_syscall(filter, SYS_open_by_handle_at);
#endif
#ifdef SYS_bpf
    append_seccomp_deny_syscall(filter, SYS_bpf);
#endif
#ifdef SYS_ptrace
    append_seccomp_deny_syscall(filter, SYS_ptrace);
#endif

    filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

    sock_fprog program{};
    program.len = static_cast<unsigned short>(filter.size());
    program.filter = filter.data();

    if (seccomp_raw(SECCOMP_SET_MODE_FILTER, 0U, &program) == 0) {
        return true;
    }

    if (errno == ENOSYS) {
        set_linux_safety_error(err, OperationStep::Execute, ENOSYS,
                               "seccomp is required but unavailable on this kernel");
        return false;
    }

    set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno,
                    "seccomp(SECCOMP_SET_MODE_FILTER)");
    return false;
}

std::string normalize_path_for_landlock(std::string path) {
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    if (path.empty()) {
        return ".";
    }
    return path;
}

std::string parent_path_for_landlock(const std::string& path) {
    const std::string normalized = normalize_path_for_landlock(path);
    const std::size_t pos = normalized.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return normalized.substr(0, pos);
}

void append_unique_path(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

void collect_landlock_paths(const Request& request, std::vector<std::string>& out_paths) {
    out_paths.clear();
    out_paths.reserve(request.sources.size() * 2 + 2);

    char cwd[PATH_MAX];
    if (::getcwd(cwd, sizeof(cwd)) != nullptr) {
        append_unique_path(out_paths, normalize_path_for_landlock(cwd));
    }

    for (const std::string& source : request.sources) {
        const std::string normalized_source = normalize_path_for_landlock(source);
        append_unique_path(out_paths, normalized_source);
        append_unique_path(out_paths, parent_path_for_landlock(normalized_source));
    }

    if (is_copy_like(request.operation)) {
        const std::string normalized_destination = normalize_path_for_landlock(request.destination.targetDir);
        append_unique_path(out_paths, normalized_destination);
        append_unique_path(out_paths, parent_path_for_landlock(normalized_destination));
    }
}

bool is_landlock_unavailable_error(int errnum) {
    return errnum == ENOSYS || errnum == EOPNOTSUPP || errnum == ENOTSUP;
}

#if defined(PCMANFM_HAVE_LANDLOCK_HEADER)
int landlock_create_ruleset_raw(const struct landlock_ruleset_attr* attr, std::size_t size, std::uint32_t flags) {
#ifdef SYS_landlock_create_ruleset
    return static_cast<int>(::syscall(SYS_landlock_create_ruleset, attr, size, flags));
#else
    (void)attr;
    (void)size;
    (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

int landlock_add_rule_raw(int ruleset_fd, enum landlock_rule_type rule_type, const void* attr, std::uint32_t flags) {
#ifdef SYS_landlock_add_rule
    return static_cast<int>(::syscall(SYS_landlock_add_rule, ruleset_fd, rule_type, attr, flags));
#else
    (void)ruleset_fd;
    (void)rule_type;
    (void)attr;
    (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

int landlock_restrict_self_raw(int ruleset_fd, std::uint32_t flags) {
#ifdef SYS_landlock_restrict_self
    return static_cast<int>(::syscall(SYS_landlock_restrict_self, ruleset_fd, flags));
#else
    (void)ruleset_fd;
    (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

std::uint64_t landlock_access_mask_for_abi(int abi_version) {
    std::uint64_t mask = 0;
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_EXECUTE);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_WRITE_FILE);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_READ_FILE);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_READ_DIR);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_REMOVE_DIR);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_REMOVE_FILE);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_CHAR);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_DIR);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_REG);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_SOCK);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_FIFO);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_BLOCK);
    mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_MAKE_SYM);
#ifdef LANDLOCK_ACCESS_FS_REFER
    if (abi_version >= 2) {
        mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_REFER);
    }
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi_version >= 3) {
        mask |= static_cast<std::uint64_t>(LANDLOCK_ACCESS_FS_TRUNCATE);
    }
#endif
    return mask;
}
#endif

bool apply_landlock(const Request& request, bool required, Error& err) {
#if !defined(PCMANFM_HAVE_LANDLOCK_HEADER)
    if (required) {
        set_linux_safety_error(err, OperationStep::Execute, ENOSYS,
                               "Landlock is required but headers are unavailable in this build");
        return false;
    }
    (void)request;
    return true;
#else
    const int abi_version = landlock_create_ruleset_raw(nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi_version < 0) {
        if (!required && is_landlock_unavailable_error(errno)) {
            return true;
        }

        if (is_landlock_unavailable_error(errno)) {
            set_linux_safety_error(err, OperationStep::Execute, ENOSYS,
                                   "Landlock is required but unavailable on this kernel");
            return false;
        }

        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno,
                        "landlock_create_ruleset(VERSION)");
        return false;
    }

    const std::uint64_t handled_access_mask = landlock_access_mask_for_abi(abi_version);
    if (handled_access_mask == 0) {
        if (required) {
            set_error(err, EngineErrorCode::SafetyRequirementUnavailable, OperationStep::Execute, ENOSYS,
                      "Landlock rights mask is empty for this ABI");
            return false;
        }
        return true;
    }

    struct landlock_ruleset_attr ruleset_attr{};
    ruleset_attr.handled_access_fs = handled_access_mask;

    LinuxFsSafety::Fd ruleset_fd(landlock_create_ruleset_raw(&ruleset_attr, sizeof(ruleset_attr), 0));
    if (!ruleset_fd.valid()) {
        if (!required && is_landlock_unavailable_error(errno)) {
            return true;
        }

        if (is_landlock_unavailable_error(errno)) {
            set_linux_safety_error(err, OperationStep::Execute, ENOSYS,
                                   "Landlock is required but unavailable on this kernel");
            return false;
        }

        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno,
                        "landlock_create_ruleset");
        return false;
    }

    std::vector<std::string> allowed_paths;
    collect_landlock_paths(request, allowed_paths);
    for (const std::string& path : allowed_paths) {
        LinuxFsSafety::Fd path_fd(::open(path.c_str(), O_PATH | O_CLOEXEC));
        if (!path_fd.valid()) {
            set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno, "open(O_PATH)");
            return false;
        }

        struct landlock_path_beneath_attr path_rule{};
        path_rule.allowed_access = handled_access_mask;
        path_rule.parent_fd = path_fd.get();

        if (landlock_add_rule_raw(ruleset_fd.get(), LANDLOCK_RULE_PATH_BENEATH, &path_rule, 0) != 0) {
            set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno, "landlock_add_rule");
            return false;
        }
    }

    if (landlock_restrict_self_raw(ruleset_fd.get(), 0) != 0) {
        if (!required && is_landlock_unavailable_error(errno)) {
            return true;
        }
        if (is_landlock_unavailable_error(errno)) {
            set_linux_safety_error(err, OperationStep::Execute, ENOSYS,
                                   "Landlock is required but unavailable on this kernel");
            return false;
        }

        set_errno_error(err, EngineErrorCode::OperationFailed, OperationStep::Execute, errno, "landlock_restrict_self");
        return false;
    }

    return true;
#endif
}

bool apply_worker_sandbox(const Request& request, Error& err) {
    if (!set_no_new_privs(err)) {
        return false;
    }
    if (!install_seccomp_filter(err)) {
        return false;
    }
    if (!apply_landlock(request, request.linuxSafety.requireLandlock, err)) {
        return false;
    }
    return true;
}

bool canonicalize_local_plan_paths(const Request& request, std::vector<SourcePlan>& plans, Error& err) {
    for (SourcePlan& plan : plans) {
        std::string canonical_source;
        if (!canonicalize_path_parent_for_local_execution(plan.sourcePath, canonical_source, err)) {
            return false;
        }
        plan.sourcePath = std::move(canonical_source);

        if (is_copy_like(request.operation)) {
            std::string canonical_destination;
            if (!canonicalize_path_parent_for_local_execution(plan.destinationPath, canonical_destination, err)) {
                return false;
            }
            plan.destinationPath = std::move(canonical_destination);
        }
    }

    return true;
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

EndpointKind source_endpoint_kind(const Request& request, std::size_t index) {
    if (request.routing.sourceKinds.empty()) {
        return EndpointKind::NativePath;
    }
    return request.routing.sourceKinds[index];
}

Backend source_backend_selector(const Request& request, std::size_t index) {
    if (request.routing.sourceBackends.empty()) {
        return request.routing.defaultBackend;
    }
    return request.routing.sourceBackends[index];
}

Backend destination_backend_selector(const Request& request) {
    if (request.routing.destinationBackend != Backend::Auto) {
        return request.routing.destinationBackend;
    }
    return request.routing.defaultBackend;
}

bool resolve_backend_for_endpoint(EndpointKind kind,
                                  Backend selector,
                                  const std::string& endpoint_name,
                                  Backend& out_backend,
                                  Error& err) {
    switch (selector) {
        case Backend::Auto:
            out_backend = (kind == EndpointKind::Uri) ? Backend::Gio : Backend::LocalHardened;
            return true;
        case Backend::LocalHardened:
            if (kind == EndpointKind::Uri) {
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                          endpoint_name + " uses URI endpoint kind but forces LocalHardened backend");
                return false;
            }
            out_backend = Backend::LocalHardened;
            return true;
        case Backend::Gio:
            out_backend = Backend::Gio;
            return true;
    }

    set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
              endpoint_name + " has an unknown backend selector");
    return false;
}

bool validate_backend_capabilities(Backend backend,
                                   EndpointKind endpoint_kind,
                                   Operation operation,
                                   const std::string& endpoint_name,
                                   Error& err) {
    const BackendCapabilities* backend_capabilities = backend_capabilities_for(backend);
    if (!backend_capabilities) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                  endpoint_name + " resolved to an unknown backend");
        return false;
    }

    if (!backend_capabilities->available) {
        std::string message = std::string(backend_name(backend)) + " backend is unavailable";
        if (!backend_capabilities->unavailableReason.empty()) {
            message += ": " + backend_capabilities->unavailableReason;
        }
        set_error(err, EngineErrorCode::UnsupportedFeature, OperationStep::ValidateRequest, ENOTSUP, message);
        return false;
    }

    if (endpoint_kind == EndpointKind::NativePath && !backend_capabilities->supportsNativePaths) {
        set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                  endpoint_name + " is a native path but backend does not support native paths");
        return false;
    }

    if (endpoint_kind == EndpointKind::Uri && !backend_capabilities->supportsUriPaths) {
        set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                  endpoint_name + " is a URI but backend does not support URIs");
        return false;
    }

    if (!backend_supports_operation(*backend_capabilities, operation)) {
        set_error(err, EngineErrorCode::UnsupportedOperation, OperationStep::ValidateRequest, ENOTSUP,
                  std::string(operation_name(operation)) + " is not supported by backend " + backend_name(backend));
        return false;
    }

    return true;
}

bool merge_resolved_backend(Backend endpoint_backend, bool& has_backend, Backend& resolved_backend, Error& err) {
    if (!has_backend) {
        resolved_backend = endpoint_backend;
        has_backend = true;
        return true;
    }

    if (endpoint_backend == resolved_backend) {
        return true;
    }

    set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
              "Mixed backend routing in a single request is not supported by the current executor");
    return false;
}

bool validate_request_routing(const Request& request, Backend& resolved_backend, Error& err) {
    if (!request.routing.sourceKinds.empty() && request.routing.sourceKinds.size() != request.sources.size()) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                  "routing.sourceKinds must match source count");
        return false;
    }

    if (!request.routing.sourceBackends.empty() && request.routing.sourceBackends.size() != request.sources.size()) {
        set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                  "routing.sourceBackends must match source count");
        return false;
    }

    bool has_backend = false;
    for (std::size_t i = 0; i < request.sources.size(); ++i) {
        const EndpointKind endpoint_kind = source_endpoint_kind(request, i);
        const Backend selector = source_backend_selector(request, i);
        Backend endpoint_backend = Backend::LocalHardened;

        const std::string endpoint_name = "source[" + std::to_string(i) + "]";
        if (!resolve_backend_for_endpoint(endpoint_kind, selector, endpoint_name, endpoint_backend, err)) {
            return false;
        }
        if (!validate_backend_capabilities(endpoint_backend, endpoint_kind, request.operation, endpoint_name, err)) {
            return false;
        }
        if (!merge_resolved_backend(endpoint_backend, has_backend, resolved_backend, err)) {
            return false;
        }
    }

    if (is_copy_like(request.operation)) {
        const EndpointKind endpoint_kind = request.routing.destinationKind;
        const Backend selector = destination_backend_selector(request);
        Backend endpoint_backend = Backend::LocalHardened;
        const std::string endpoint_name = "destination";
        if (!resolve_backend_for_endpoint(endpoint_kind, selector, endpoint_name, endpoint_backend, err)) {
            return false;
        }
        if (!validate_backend_capabilities(endpoint_backend, endpoint_kind, request.operation, endpoint_name, err)) {
            return false;
        }
        if (!merge_resolved_backend(endpoint_backend, has_backend, resolved_backend, err)) {
            return false;
        }
    }

    return has_backend;
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

    switch (request.linuxSafety.workerMode) {
        case WorkerMode::InProcess:
            if (request.linuxSafety.requireLandlock) {
                set_error(err, EngineErrorCode::SafetyRequirementUnavailable, OperationStep::ValidateRequest, ENOSYS,
                          "Landlock was requested but sandboxed worker mode is not enabled");
                return false;
            }
            if (request.linuxSafety.requireSeccomp) {
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ValidateRequest, ENOTSUP,
                          "requireSeccomp requires sandboxed worker mode");
                return false;
            }
            break;
        case WorkerMode::SandboxedThread:
            break;
        default:
            set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                      "Unknown worker mode");
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
        case Operation::Trash:
        case Operation::Untrash:
            if (!request.destination.targetDir.empty()) {
                set_error(err, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
                          "Trash and Untrash requests must not set destination.targetDir");
                return false;
            }
            break;
        case Operation::Mkdir:
        case Operation::Link:
            set_error(err, EngineErrorCode::UnsupportedOperation, OperationStep::ValidateRequest, ENOTSUP,
                      "Operation is not yet supported by the unified contract executor");
            return false;
    }

    Backend request_backend = Backend::LocalHardened;
    if (!validate_request_routing(request, request_backend, err)) {
        return false;
    }

    if (request_backend == Backend::LocalHardened && request.linuxSafety.requireOpenat2Resolve &&
        !probe_openat2_resolve(err)) {
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

bool choose_rename_destination(const std::string& base_destination, std::string& renamed_destination, Error& err) {
    std::string parent;
    std::string name;
    if (!split_parent_and_name(base_destination, parent, name)) {
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
        const std::string candidate_name = stem + suffix + extension;
        const std::string candidate = join_path(parent, candidate_name);

        bool exists = false;
        bool is_dir = false;
        if (!destination_exists(candidate, exists, is_dir, err)) {
            return false;
        }
        if (!exists) {
            renamed_destination = candidate;
            return true;
        }
    }

    set_error(err, EngineErrorCode::OperationFailed, OperationStep::ResolveConflict, EEXIST,
              "Unable to find a unique destination name");
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
                               const std::string& preferred_destination,
                               std::string& resolved_destination,
                               ConflictResolutionState& resolution_state,
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
    if (!destination_exists(resolved_destination, exists, is_dir, err)) {
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
            case ConflictPolicy::Rename:
                // Require no-overwrite so late races are handled via rename policy.
                allow_overwrite = false;
                return true;
            case ConflictPolicy::Prompt:
                if (!resolution_state.hasApplyToAllResolution) {
                    // Defer decision until an actual conflict exists.
                    allow_overwrite = false;
                    return true;
                }

                switch (resolution_state.applyToAllResolution) {
                    case ConflictResolution::Overwrite:
                        allow_overwrite = true;
                        return true;
                    case ConflictResolution::Skip:
                    case ConflictResolution::Rename:
                        allow_overwrite = false;
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
            if (resolution_state.hasApplyToAllResolution) {
                resolution = resolution_state.applyToAllResolution;
                break;
            }
            if (!handlers.onConflict) {
                set_error(err, EngineErrorCode::UnsupportedPolicy, OperationStep::ResolveConflict, ENOTSUP,
                          "Prompt conflict policy requires an onConflict handler");
                return false;
            }

            {
                const ConflictResolution raw_resolution = handlers.onConflict(ConflictEvent{
                    ConflictKind::DestinationExists,
                    plan.sourcePath,
                    resolved_destination,
                    is_dir,
                });
                bool apply_to_all = false;
                if (!normalize_conflict_resolution(raw_resolution, resolution, apply_to_all, err)) {
                    return false;
                }
                if (apply_to_all) {
                    resolution_state.hasApplyToAllResolution = true;
                    resolution_state.applyToAllResolution = resolution;
                }
            }
            break;
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
            if (!choose_rename_destination(preferred_destination, resolved_destination, err)) {
                return false;
            }
            skip = false;
            allow_overwrite = false;
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

bool execute_plan_operation(const Request& request,
                            const SourcePlan& plan,
                            const std::string& destination_path,
                            FsOps::ProgressInfo& source_progress,
                            const FsOps::ProgressCallback& callback,
                            FsOps::Error& fs_err,
                            bool overwrite_existing) {
    switch (request.operation) {
        case Operation::Copy:
            return FsOps::copy_path(plan.sourcePath, destination_path, source_progress, callback, fs_err,
                                    request.metadata.preserveOwnership, overwrite_existing);
        case Operation::Move:
            return FsOps::move_path(plan.sourcePath, destination_path, source_progress, callback, fs_err,
                                    /*forceCopyFallbackForTests=*/false, request.metadata.preserveOwnership,
                                    overwrite_existing);
        case Operation::Delete:
            return FsOps::delete_path(plan.sourcePath, source_progress, callback, fs_err);
        case Operation::Trash:
        case Operation::Untrash:
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

Result run_in_process(const Request& request, const EventHandlers& handlers) {
    Result result;

    if (is_cancel_requested(request)) {
        result.cancelled = true;
        set_cancelled(result.error, OperationStep::ValidateRequest);
        return result;
    }

    if (!validate_request(request, result.error)) {
        return result;
    }

    Backend request_backend = Backend::LocalHardened;
    Error routing_error;
    if (!validate_request_routing(request, request_backend, routing_error)) {
        result.error = routing_error;
        return result;
    }

    if (request_backend == Backend::Gio) {
        return detail::run_gio_request(request, handlers);
    }

    std::vector<SourcePlan> plans;
    ProgressSnapshot totals;
    if (!build_plan(request, plans, totals, result.error)) {
        return result;
    }
    if (!canonicalize_local_plan_paths(request, plans, result.error)) {
        return result;
    }

    ProgressSnapshot last_progress;
    last_progress.filesTotal = totals.filesTotal;
    last_progress.bytesTotal = totals.bytesTotal;

    int completed_units = 0;
    std::uint64_t completed_bytes = 0;
    ConflictResolutionState conflict_resolution_state;

    for (const SourcePlan& plan : plans) {
        if (is_cancel_requested(request)) {
            result.cancelled = true;
            set_cancelled(result.error, OperationStep::Execute);
            result.finalProgress = last_progress;
            return result;
        }

        std::string execution_destination = plan.destinationPath;
        bool skip = false;
        bool allow_overwrite = true;
        Error conflict_error;
        if (!resolve_conflict_for_plan(request, handlers, plan, plan.destinationPath, execution_destination,
                                       conflict_resolution_state, skip, allow_overwrite, conflict_error)) {
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
        bool executed = execute_plan_operation(request, plan, execution_destination, source_progress, progress_cb,
                                               fs_err, allow_overwrite);
        if (!executed && is_copy_like(request.operation) && fs_err.code == EEXIST && !allow_overwrite) {
            bool late_skip = false;
            bool late_allow_overwrite = false;
            const std::string attempted_destination = execution_destination;
            Error late_conflict_error;
            if (!resolve_conflict_for_plan(request, handlers, plan, plan.destinationPath, execution_destination,
                                           conflict_resolution_state, late_skip, late_allow_overwrite,
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

            if (late_allow_overwrite || execution_destination != attempted_destination) {
                reset_source_progress();
                fs_err = {};
                executed = execute_plan_operation(request, plan, execution_destination, source_progress, progress_cb,
                                                  fs_err, late_allow_overwrite);
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

struct SandboxedThreadEvent {
    enum class Kind {
        Progress,
        Conflict,
    };

    Kind kind = Kind::Progress;
    ProgressSnapshot progress;
    ConflictEvent conflict;
    std::uint64_t conflictId = 0;
};

struct SandboxedThreadState {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<SandboxedThreadEvent> pendingEvents;
    bool workerDone = false;
    Result workerResult;
    std::uint64_t nextConflictId = 1;
    bool conflictResponseReady = false;
    std::uint64_t conflictResponseId = 0;
    ConflictResolution conflictResponse = ConflictResolution::Abort;
};

Result run_sandboxed_thread(const Request& request, const EventHandlers& handlers) {
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    SandboxedThreadState state;

    std::thread worker([&request, &handlers, &state, cancelled]() {
        Result worker_result;
        Request worker_request = request;
        worker_request.linuxSafety.workerMode = WorkerMode::InProcess;
        worker_request.linuxSafety.requireLandlock = false;
        worker_request.linuxSafety.requireSeccomp = false;
        worker_request.cancellationRequested = [cancelled]() { return cancelled->load(); };

        EventHandlers worker_handlers;
        if (handlers.onProgress) {
            worker_handlers.onProgress = [&state](const ProgressSnapshot& update) {
                std::lock_guard<std::mutex> lock(state.mutex);
                SandboxedThreadEvent event;
                event.kind = SandboxedThreadEvent::Kind::Progress;
                event.progress = update;
                state.pendingEvents.push_back(std::move(event));
                state.cv.notify_all();
            };
        }

        if (handlers.onConflict) {
            worker_handlers.onConflict = [&state, cancelled](const ConflictEvent& event) -> ConflictResolution {
                std::unique_lock<std::mutex> lock(state.mutex);
                const std::uint64_t conflict_id = state.nextConflictId++;

                SandboxedThreadEvent queued;
                queued.kind = SandboxedThreadEvent::Kind::Conflict;
                queued.conflict = event;
                queued.conflictId = conflict_id;
                state.pendingEvents.push_back(std::move(queued));
                state.cv.notify_all();

                state.cv.wait(lock, [&state, conflict_id, &cancelled]() {
                    return cancelled->load() ||
                           (state.conflictResponseReady && state.conflictResponseId == conflict_id);
                });

                if (cancelled->load()) {
                    return ConflictResolution::Abort;
                }

                const ConflictResolution response = state.conflictResponse;
                state.conflictResponseReady = false;
                return response;
            };
        }

        if (cancelled->load()) {
            worker_result.cancelled = true;
            set_cancelled(worker_result.error, OperationStep::ValidateRequest);
        }
        else {
            Error sandbox_error;
            if (!apply_worker_sandbox(request, sandbox_error)) {
                worker_result.success = false;
                worker_result.cancelled = (sandbox_error.sysErrno == ECANCELED);
                worker_result.error = sandbox_error;
            }
            else {
                worker_result = run_in_process(worker_request, worker_handlers);
            }
        }

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.workerResult = std::move(worker_result);
            state.workerDone = true;
        }
        state.cv.notify_all();
    });

    Result result;
    for (;;) {
        if (!cancelled->load() && is_cancel_requested(request)) {
            cancelled->store(true);
            state.cv.notify_all();
        }

        std::deque<SandboxedThreadEvent> events;
        bool worker_done = false;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.cv.wait_for(lock, std::chrono::milliseconds(20),
                              [&state]() { return state.workerDone || !state.pendingEvents.empty(); });
            events.swap(state.pendingEvents);
            worker_done = state.workerDone;
        }

        for (const SandboxedThreadEvent& event : events) {
            if (event.kind == SandboxedThreadEvent::Kind::Progress) {
                if (handlers.onProgress) {
                    handlers.onProgress(event.progress);
                }
            }
            else if (event.kind == SandboxedThreadEvent::Kind::Conflict) {
                ConflictResolution response = ConflictResolution::Abort;
                if (handlers.onConflict) {
                    response = handlers.onConflict(event.conflict);
                }
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.conflictResponse = response;
                    state.conflictResponseId = event.conflictId;
                    state.conflictResponseReady = true;
                }
                state.cv.notify_all();
            }

            if (!cancelled->load() && is_cancel_requested(request)) {
                cancelled->store(true);
                state.cv.notify_all();
            }
        }

        if (worker_done && events.empty()) {
            break;
        }
    }

    if (worker.joinable()) {
        worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        result = state.workerResult;
    }
    return result;
}

}  // namespace

CapabilityReport capabilities() {
    return capability_report();
}

Result preflight(const Request& request) {
    Result result;

    if (is_cancel_requested(request)) {
        result.cancelled = true;
        set_cancelled(result.error, OperationStep::ValidateRequest);
        return result;
    }

    if (!validate_request(request, result.error)) {
        return result;
    }

    result.success = true;
    return result;
}

Result run(const Request& request, const EventHandlers& handlers) {
    switch (request.linuxSafety.workerMode) {
        case WorkerMode::InProcess:
            return run_in_process(request, handlers);
        case WorkerMode::SandboxedThread:
            return run_sandboxed_thread(request, handlers);
        default:
            break;
    }

    Result result;
    set_error(result.error, EngineErrorCode::InvalidRequest, OperationStep::ValidateRequest, EINVAL,
              "Unknown worker mode");
    return result;
}

}  // namespace Oneg4FM::FileOpsContract
