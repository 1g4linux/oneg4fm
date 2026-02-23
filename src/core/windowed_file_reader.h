/*
 * Sliding mmap-based reader for large files (POSIX-only, no Qt)
 * src/core/windowed_file_reader.h
 */

#ifndef PCMANFM_WINDOWED_FILE_READER_H
#define PCMANFM_WINDOWED_FILE_READER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Oneg4FM {

// WindowedFileReader maps a moving window of the file. When mmap is unavailable,
// it transparently falls back to pread() into an internal buffer.
class WindowedFileReader {
   public:
    WindowedFileReader(const std::string& path, std::size_t windowSizeBytes, std::string* errorOut = nullptr);
    ~WindowedFileReader();

    WindowedFileReader(const WindowedFileReader&) = delete;
    WindowedFileReader& operator=(const WindowedFileReader&) = delete;

    std::size_t size() const { return fileSize_; }
    bool valid() const { return valid_; }
    const std::string& lastError() const { return lastError_; }

    // Reads up to length bytes starting at offset into dest. bytesReadOut reports
    // how many bytes were actually copied (short if near EOF). On failure returns
    // false and fills errorOut.
    bool read(std::uint64_t offset,
              std::size_t length,
              std::uint8_t* dest,
              std::size_t& bytesReadOut,
              std::string& errorOut) const;

   private:
    struct Window {
        void* ptr = nullptr;
        std::size_t length = 0;
        std::uint64_t offset = 0;
        bool isMmap = false;
    };

    bool mapWindow(std::uint64_t offset, std::size_t length, std::string& errorOut) const;
    bool fillFallback(std::uint64_t offset, std::size_t length, std::string& errorOut) const;
    void clearWindow() const;

    int fd_ = -1;
    bool valid_ = false;
    std::string lastError_;
    std::size_t fileSize_ = 0;
    std::size_t pageSize_ = 4096;
    std::size_t windowSize_ = 0;
    mutable Window window_;
    mutable std::vector<std::uint8_t> fallback_;
    mutable std::mutex windowMutex_;
};

}  // namespace Oneg4FM

#endif  // PCMANFM_WINDOWED_FILE_READER_H
