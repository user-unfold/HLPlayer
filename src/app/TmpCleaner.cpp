#include "TmpCleaner.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hlplayer::app {

CleanupResult TmpCleaner::cleanupTmpFiles() {
    CleanupResult result;

    // Common export directories to scan
    std::vector<std::string> scanDirs = {
        ".",  // Current directory
        "../output",  // Common output folder
        "../export",  // Alternative export folder
    };

    for (const auto& dir : scanDirs) {
        CleanupResult dirResult = scanDirectory(dir);
        result.filesCleaned += dirResult.filesCleaned;
        result.cleanedPaths.insert(result.cleanedPaths.end(),
                                   dirResult.cleanedPaths.begin(),
                                   dirResult.cleanedPaths.end());
        result.errors.insert(result.errors.end(),
                            dirResult.errors.begin(),
                            dirResult.errors.end());
    }

    if (result.filesCleaned > 0) {
        std::cout << "[TmpCleaner] Cleaned " << result.filesCleaned
                  << " leftover .hlv.tmp file(s)" << std::endl;
    }

    return result;
}

CleanupResult TmpCleaner::scanDirectory(const std::string& dirPath) {
#ifdef _WIN32
    // Convert to UTF-16 for Windows API
    std::wstring wpath(dirPath.begin(), dirPath.end());
    return scanDirectoryWindows(wpath);
#else
    return scanDirectoryUnix(dirPath);
#endif
}

#ifdef _WIN32
CleanupResult TmpCleaner::scanDirectoryWindows(const std::wstring& dirPath) {
    CleanupResult result;

    // Build search pattern: dirPath\*.hlv.tmp
    std::wstring searchPattern = dirPath + L"\\*.hlv.tmp";

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            // Only log errors if it's not "file not found" (which is normal for empty dirs)
            std::string narrowPath(dirPath.begin(), dirPath.end());
            result.errors.push_back("Failed to scan directory: " + narrowPath +
                                   " (error code: " + std::to_string(error) + ")");
        }
        return result;
    }

    do {
        const std::wstring& filename = findData.cFileName;
        if (wcscmp(filename.c_str(), L".") == 0 || wcscmp(filename.c_str(), L"..") == 0) {
            continue;  // Skip . and ..
        }

        // Build full path
        std::wstring fullPath = dirPath + L"\\" + filename;

        // Delete the file
        if (DeleteFileW(fullPath.c_str())) {
            std::string narrowPath(fullPath.begin(), fullPath.end());
            result.cleanedPaths.push_back(narrowPath);
            result.filesCleaned++;
        } else {
            DWORD error = GetLastError();
            std::string narrowPath(fullPath.begin(), fullPath.end());
            result.errors.push_back("Failed to delete: " + narrowPath +
                                   " (error code: " + std::to_string(error) + ")");
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return result;
}
#else
CleanupResult TmpCleaner::scanDirectoryUnix(const std::string& dirPath) {
    CleanupResult result;

    DIR* dir = opendir(dirPath.c_str());
    if (dir == nullptr) {
        int error = errno;
        if (error != ENOENT) {
            // Only log errors if it's not "directory not found"
            result.errors.push_back("Failed to open directory: " + dirPath +
                                   " (error code: " + std::to_string(error) + ")");
        }
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* filename = entry->d_name;
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
            continue;  // Skip . and ..
        }

        // Check if filename ends with .hlv.tmp
        size_t len = strlen(filename);
        if (len >= 8 && strcmp(filename + len - 8, ".hlv.tmp") == 0) {
            // Build full path
            std::string fullPath = dirPath + "/" + filename;

            // Delete the file
            if (unlink(fullPath.c_str()) == 0) {
                result.cleanedPaths.push_back(fullPath);
                result.filesCleaned++;
            } else {
                int error = errno;
                result.errors.push_back("Failed to delete: " + fullPath +
                                       " (error code: " + std::to_string(error) + ")");
            }
        }
    }

    closedir(dir);
    return result;
}
#endif

} // namespace hlplayer::app