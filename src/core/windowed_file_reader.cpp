/*
 * Sliding mmap-based reader for large files (POSIX-only, no Qt)
 * src/core/windowed_file_reader.cpp
 */

#include "windowed_file_reader.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace Oneg4FM {

namespace {

std::string errnoString(const char* context) {
    return std::string(context) + ": " + std::strerror(errno);
}

}  // namespace

WindowedFileReader::WindowedFileReader(const std::string& path, std::size_t windowSizeBytes, std::string* errorOut)
    : windowSize_(windowSizeBytes) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd_ < 0) {
        lastError_ = errnoString("open");
        if (errorOut) {
            *errorOut = lastError_;
        }
        return;
    }

    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        lastError_ = errnoString("fstat");
        ::close(fd_);
        fd_ = -1;
        if (errorOut) {
            *errorOut = lastError_;
        }
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        lastError_ = "not a regular file";
        ::close(fd_);
        fd_ = -1;
        if (errorOut) {
            *errorOut = lastError_;
        }
        return;
    }

    if (st.st_size < 0) {
        lastError_ = "negative file size";
        ::close(fd_);
        fd_ = -1;
        if (errorOut) {
            *errorOut = lastError_;
        }
        return;
    }
    fileSize_ = static_cast<std::size_t>(st.st_size);

    const long page = ::sysconf(_SC_PAGESIZE);
    pageSize_ = page > 0 ? static_cast<std::size_t>(page) : 4096;
    if (windowSize_ == 0) {
        windowSize_ = pageSize_ * 256;  // default ~1 MiB
    }
    windowSize_ = ((windowSize_ + pageSize_ - 1) / pageSize_) * pageSize_;

    fallback_.resize(windowSize_);
    valid_ = true;
}

WindowedFileReader::~WindowedFileReader() {
    clearWindow();
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void WindowedFileReader::clearWindow() const {
    if (window_.isMmap && window_.ptr) {
        ::munmap(window_.ptr, window_.length);
    }
    window_.ptr = nullptr;
    window_.length = 0;
    window_.offset = 0;
    window_.isMmap = false;
}

bool WindowedFileReader::mapWindow(std::uint64_t offset, std::size_t length, std::string& errorOut) const {
    if (!valid_) {
        errorOut = lastError_;
        return false;
    }
    clearWindow();
    if (fileSize_ == 0) {
        return true;
    }
    const std::uint64_t aligned = (offset / pageSize_) * pageSize_;
    const std::size_t maxLen = std::min<std::size_t>(windowSize_, fileSize_ - aligned);
    if (maxLen == 0) {
        return true;
    }

    void* m = ::mmap(nullptr, maxLen, PROT_READ, MAP_SHARED, fd_, static_cast<off_t>(aligned));
    if (m == MAP_FAILED) {
        return fillFallback(offset, length, errorOut);
    }

    window_.ptr = m;
    window_.length = maxLen;
    window_.offset = aligned;
    window_.isMmap = true;

    return true;
}

bool WindowedFileReader::fillFallback(std::uint64_t offset, std::size_t /*length*/, std::string& errorOut) const {
    if (!valid_) {
        errorOut = lastError_;
        return false;
    }
    clearWindow();
    if (fileSize_ == 0) {
        return true;
    }

    const std::uint64_t aligned = (offset / pageSize_) * pageSize_;
    const std::size_t maxLen = std::min<std::size_t>(windowSize_, fileSize_ - aligned);
    if (fallback_.size() < maxLen) {
        fallback_.resize(maxLen);
    }

    std::size_t filled = 0;
    while (filled < maxLen) {
        const ssize_t n =
            ::pread(fd_, fallback_.data() + filled, maxLen - filled, static_cast<off_t>(aligned + filled));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            errorOut = errnoString("pread");
            return false;
        }
        if (n == 0) {
            break;
        }
        filled += static_cast<std::size_t>(n);
    }

    window_.ptr = fallback_.data();
    window_.length = filled;
    window_.offset = aligned;
    window_.isMmap = false;
    return true;
}

bool WindowedFileReader::read(std::uint64_t offset,
                              std::size_t length,
                              std::uint8_t* dest,
                              std::size_t& bytesReadOut,
                              std::string& errorOut) const {
    if (!valid_) {
        errorOut = lastError_;
        return false;
    }
    bytesReadOut = 0;
    if (offset >= fileSize_ || length == 0) {
        return true;
    }

    std::lock_guard<std::mutex> guard(windowMutex_);

    std::size_t remaining = std::min<std::size_t>(length, fileSize_ - offset);
    std::uint64_t pos = offset;
    std::size_t filled = 0;

    while (remaining > 0) {
        const bool needRemap = !window_.ptr || pos < window_.offset || pos >= window_.offset + window_.length;
        if (needRemap) {
            if (!mapWindow(pos, remaining, errorOut)) {
                return false;
            }
            if (!window_.ptr || window_.length == 0) {
                break;
            }
        }

        const std::size_t windowOff = static_cast<std::size_t>(pos - window_.offset);
        const std::size_t chunk = std::min<std::size_t>(remaining, window_.length - windowOff);
        std::memcpy(dest + filled, static_cast<std::uint8_t*>(window_.ptr) + windowOff, chunk);
        filled += chunk;
        pos += chunk;
        remaining -= chunk;
    }

    bytesReadOut = filled;
    return true;
}

}  // namespace Oneg4FM
