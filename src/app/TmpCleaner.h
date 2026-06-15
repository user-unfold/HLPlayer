#ifndef HLPLAYER_TMP_CLEANER_H
#define HLPLAYER_TMP_CLEANER_H

#include <string>
#include <vector>

namespace hlplayer::app {

// Result of tmp file cleanup
struct CleanupResult {
    int filesCleaned = 0;
    std::vector<std::string> cleanedPaths;
    std::vector<std::string> errors;
};

// Clean up leftover .hlv.tmp files from encryption operations
class TmpCleaner {
public:
    // Scan common export directories for *.hlv.tmp files and delete them
    static CleanupResult cleanupTmpFiles();

private:
    // Scan a specific directory for .hlv.tmp files
    static CleanupResult scanDirectory(const std::string& dirPath);

#ifdef _WIN32
    // Windows-specific: find and delete .hlv.tmp files
    static CleanupResult scanDirectoryWindows(const std::wstring& dirPath);
#else
    // Unix-specific: find and delete .hlv.tmp files
    static CleanupResult scanDirectoryUnix(const std::string& dirPath);
#endif
};

} // namespace hlplayer::app

#endif // HLPLAYER_TMP_CLEANER_H