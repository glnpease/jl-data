#pragma once
#include <string>
#include <vector>
#include <unordered_set>

class Git {
public:

    struct FileInfo {
        std::string filename;
        int created;

        FileInfo(std::string const & filename, int created):
            filename(filename),
            created(created) {
        }
    };

    struct FileHistory {
        std::string filename; // the filename may change when renamed, etc. 
        int date;
        std::string hash;
        FileHistory(std::string const & filename,  int date, std::string const & hash):
            filename(filename),
            date(date),
            hash(hash) {
        }
    };

    struct BranchInfo {
        std::string name;
        std::string commit;
        int date;

        BranchInfo(std::string const & name, std::string const & commit, int date):
            name(name),
            commit(commit),
            date(date) {
        }

    };

    /** Clones the given repository to specified path.

      Returns true if successful. The destination path must not exist when calling the function.
     */
    static bool Clone(std::string const & url, std::string const & into);

    /** Returns list of all branches in the given repository.
     */
    static std::unordered_set<std::string> GetBranches(std::string const & repoPath);

    /** Returns the current branch of the github repository.
     */
    static std::string GetCurrentBranch(std::string const & repoPath);

    /** Returns the latest commit on current branch.
     */
    static std::string GetLatestCommit(std::string const & repoPath);

    /** Checkouts the given branch.

      Returns true if successful, false otherwise.
     */
    static bool SetBranch(std::string const & repoPath, std::string const branch);

    /** Returns the info for current branch.
     */
    static BranchInfo GetBranchInfo(std::string const & repoPath);

    /** Returns list of all files from given repository (including the deleted ones).
     */
    static std::vector<FileInfo> GetFileInfo(std::string const & repoPath);

    /** For a given file, returns the list of its revisions.
     */
    static std::vector<FileHistory> GetFileHistory(std::string const & repoPath, FileInfo const & file);

    /** For a given file history, stores the contents of the given file at that level into the specified string (into). If the file does not exist at that revision, does not change the string and returns false. 
     */
    static bool GetFileRevision(std::string const & repoPath,  const FileHistory & file, std::string & into);








};
