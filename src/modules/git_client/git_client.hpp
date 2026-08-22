#pragma once

// Git client module for Material Everything.
//
// Wraps the git CLI behind a small, UI-friendly API. Every operation is
// synchronous and returns a Result; the desktop layer decides how to surface
// progress. Paths are repository-relative unless a parameter is explicitly
// absolute.

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace me::gitclient {

struct FileStatus {
    std::string path;
    bool staged = false;
    bool modified = false;
    bool untracked = false;
    bool deleted = false;
};

struct CommitInfo {
    std::string hash;
    std::vector<std::string> parents;
    std::string author;
    std::string email;
    std::string date;
    std::string subject;
    int graphRow = 0;
    int graphColumn = 0;
};

struct DiffLine {
    enum class Kind { Context, Added, Removed, HunkHeader };
    Kind kind = Kind::Context;
    std::string text;
};

struct Diff {
    std::string path;
    std::vector<DiffLine> lines;
};

struct BranchInfo {
    std::string name;
    std::string upstream;
    bool current = false;
    int ahead = 0;
    int behind = 0;
};

struct RemoteInfo {
    std::string name;
    std::string fetchUrl;
    std::string pushUrl;
};

struct StashEntry {
    int index = 0;
    std::string label;
};

struct BlameLine {
    int lineNumber = 0;
    std::string hash;
    std::string author;
    std::string date;
    std::string content;
};

struct ConflictHunk {
    std::string path;
    int beginLine = 0;
    std::string ours;
    std::string theirs;
};

struct MergeConflict {
    std::string path;
    std::vector<ConflictHunk> hunks;
};

// A single git command outcome: exit status plus combined output.
struct CommandResult {
    int exitCode = 0;
    std::string output;

    bool ok() const { return exitCode == 0; }
};

template <typename T>
struct Result {
    T value{};
    CommandResult raw;

    bool ok() const { return raw.ok(); }
    const std::string& error() const { return raw.output; }
};

class GitClient {
public:
    // Opens an existing repository at repoPath (absolute).
    explicit GitClient(std::string repoPath);

    // ---- Status / staging -----------------------------------------------
    Result<std::vector<FileStatus>> status() const;
    CommandResult stage(const std::vector<std::string>& paths) const;
    CommandResult stageAll() const;
    CommandResult unstage(const std::vector<std::string>& paths) const;

    // ---- Commits ---------------------------------------------------------
    struct CommitOptions {
        std::string message;
        bool amend = false;
        bool allowEmpty = false;
        bool signOff = false;
    };
    CommandResult commit(const CommitOptions& options) const;
    Result<std::vector<CommitInfo>> history(const std::string& range,
                                            int maxCount = 500) const;

    // ---- Diffs -----------------------------------------------------------
    Result<Diff> diffFile(const std::string& path, bool staged) const;
    Result<std::vector<Diff>> diffAll(bool staged, int maxFiles = 100) const;

    // ---- Branches --------------------------------------------------------
    Result<std::vector<BranchInfo>> branches() const;
    CommandResult checkout(const std::string& branchName,
                           bool createIfMissing = false) const;
    CommandResult createBranch(const std::string& name,
                               const std::string& startPoint = "") const;
    CommandResult deleteBranch(const std::string& name, bool force = false) const;
    CommandResult merge(const std::string& refName) const;
    CommandResult abortMerge() const;

    // ---- Merge conflicts -------------------------------------------------
    Result<std::vector<MergeConflict>> conflicts() const;
    // Writes resolvedContent to path and stages it.
    CommandResult resolveConflict(const std::string& path,
                                  const std::string& resolvedContent) const;
    CommandResult markConflictResolved(const std::string& path) const;

    // ---- Stash -----------------------------------------------------------
    Result<std::vector<StashEntry>> stashList() const;
    CommandResult stashPush(const std::string& message) const;
    CommandResult stashPop(int index) const;
    CommandResult stashApply(int index) const;
    CommandResult stashDrop(int index) const;

    // ---- Remotes ---------------------------------------------------------
    Result<std::vector<RemoteInfo>> remotes() const;
    CommandResult addRemote(const std::string& name, const std::string& url) const;
    CommandResult removeRemote(const std::string& name) const;
    CommandResult fetch(const std::string& remoteName) const;
    CommandResult pull(const std::string& remoteName,
                       const std::string& branchName) const;
    CommandResult push(const std::string& remoteName,
                       const std::string& branchName,
                       bool setUpstream = false) const;

    // ---- Blame -----------------------------------------------------------
    Result<std::vector<BlameLine>> blame(const std::string& path) const;

    const std::string& repoPath() const { return repoPath_; }

private:
    CommandResult runGit(const std::vector<std::string>& args) const;
    static std::vector<std::string> splitLines(const std::string& text);
    static std::vector<std::string> splitOnNul(const std::string& text);
    static std::string trim(const std::string& s);

    std::string repoPath_;
    mutable std::unordered_map<std::string, std::string> memo_;
};

}  // namespace me::gitclient
