#include "git_client.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#define popen_impl _popen
#define pclose_impl _pclose
#else
#define popen_impl popen
#define pclose_impl pclose
#endif

namespace me::gitclient {
namespace {

std::string quoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos &&
        arg.find("--") != 0) {
        return arg;
    }
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    out += '"';
    return out;
}

CommandResult runProcess(const std::string& cmdline) {
    FILE* pipe = popen_impl(cmdline.c_str(), "r");
    if (!pipe) return {-1, "popen failed"};
    std::ostringstream ss;
    std::array<char, 4096> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        ss.write(buf.data(), strlen(buf.data()));
    }
    int rc = pclose_impl(pipe);
    return {rc == -1 ? -1 : rc >> 8, ss.str()};
}

}  // namespace

GitClient::GitClient(std::string repoPath) : repoPath_(std::move(repoPath)) {}

CommandResult GitClient::runGit(
    const std::vector<std::string>& args) const {
    std::string cmd = "cd \"" + repoPath_ + "\" && git";
    for (const auto& a : args) cmd += " " + quoteArg(a);
    return runProcess(cmd);
}

Result<std::vector<FileStatus>> GitClient::status() const {
    auto r = runGit({"status", "--porcelain=v1", "-z"});
    Result<std::vector<FileStatus>> result{ {}, r };
    if (!r.ok()) return result;

    auto entries = splitOnNul(r.output);
    for (size_t i = 0; i + 1 < entries.size(); i += 2) {
        const std::string& code = entries[i];
        FileStatus fs;
        fs.path = entries[i + 1];
        char x = code.size() > 0 ? code[0] : ' ';
        char y = code.size() > 1 ? code[1] : ' ';
        fs.staged  = (x != ' ');
        fs.modified = (y == 'M' || y == 'T' || y == 'C');
        fs.untracked = (x == '?' && y == '?');
        fs.deleted = (x == 'D' || y == 'D');
        result.value.push_back(fs);
    }
    return result;
}

CommandResult GitClient::stage(const std::vector<std::string>& paths) const {
    std::vector<std::string> args{"add", "--"};
    for (auto& p : paths) args.push_back(p);
    return runGit(args);
}

CommandResult GitClient::stageAll() const { return runGit({"add", "-A"}); }

CommandResult GitClient::unstage(const std::vector<std::string>& paths) const {
    std::vector<std::string> args{"reset", "HEAD", "--"};
    for (auto& p : paths) args.push_back(p);
    return runGit(args);
}

CommandResult GitClient::commit(const CommitOptions& o) const {
    std::vector<std::string> args{"commit", "-m", o.message};
    if (o.amend)      args.push_back("--amend");
    if (o.allowEmpty) args.push_back("--allow-empty");
    if (o.signOff)    args.push_back("-s");
    return runGit(args);
}

Result<std::vector<CommitInfo>> GitClient::history(
    const std::string& range, int maxCount) const {
    std::vector<std::string> args{
        "log",
        "--pretty=format:%H%x00%P%x00%an%x00%ae%x00%aI%x00%s%x01",
        "--max-count=" + std::to_string(maxCount)};
    if (!range.empty()) args.push_back(range);
    auto r = runGit(args);
    Result<std::vector<CommitInfo>> result{ {}, r };
    if (!r.ok()) return result;

    int row = 0, col = 0;
    for (auto& line : splitLines(r.output)) {
        if (line.empty()) continue;
        line.pop_back();  // strip trailing \x01
        auto f = splitOnNul(line);
        if (f.size() < 6) continue;
        CommitInfo c;
        c.hash = f[0];
        c.parents = splitLines(f[1]);  // space-separated; single-parent common case ok
        c.author = f[2];
        c.email = f[3];
        c.date = f[4];
        c.subject = f[5];
        c.graphRow = row++;
        c.graphColumn = col;  // refined by the UI's lane builder
        result.value.push_back(c);
    }
    return result;
}

Result<Diff> GitClient::diffFile(const std::string& path, bool staged) const {
    std::vector<std::string> args{"diff", "--unified=3"};
    if (staged) args.push_back("--cached");
    args.push_back("--"); args.push_back(path);
    auto r = runGit(args);
    Result<Diff> result{ {}, r };
    if (!r.ok()) return result;

    result.value.path = path;
    for (auto& l : splitLines(r.output)) {
        DiffLine dl;
        if (l.rfind("@@", 0) == 0)      dl.kind = DiffLine::Kind::HunkHeader;
        else if (l.rfind("+++", 0)==0 || l.rfind("---",0)==0 ||
                 l.rfind("diff ",0)==0 || l.rfind("index ",0)==0)
            continue;                    // header noise
        else if (l.rfind("+",0)==0)     dl.kind = DiffLine::Kind::Added;
        else if (l.rfind("-",0)==0)     dl.kind = DiffLine::Kind::Removed;
        else                             dl.kind = DiffLine::Kind::Context;
        dl.text = l;
        result.value.lines.push_back(dl);
    }
    return result;
}

Result<std::vector<Diff>> GitClient::diffAll(bool staged, int maxFiles) const {
    Result<std::vector<Diff>> result;
    auto st = status();
    if (!st.ok()) { result.raw = st.raw; return result; }
    int n = 0;
    for (auto& fs : st.value) {
        if (fs.untracked || n >= maxFiles) continue;
        auto d = diffFile(fs.path, staged);
        if (d.ok() && !d.value.lines.empty()) result.value.push_back(d.value);
        ++n;
    }
    return result;
}

Result<std::vector<BranchInfo>> GitClient::branches() const {
    auto r = runGit({
        "for-each-ref",
        "--format=%(refname:short)%x00%(upstream:short)%x00%(HEAD)%x00"
        "%(upstream:track)",
        "refs/heads/"});
    Result<std::vector<BranchInfo>> result{ {}, r };
    if (!r.ok()) return result;

    for (auto& line : splitLines(r.output)) {
        auto f = splitOnNul(line);
        if (f.empty()) continue;
        BranchInfo b;
        b.name = f[0];
        b.upstream = f.size() > 1 ? f[1] : "";
        b.current  = f.size() > 2 && f[2] == "*";
        b.ahead = b.behind = 0;
        if (f.size() > 3) {
            // track looks like [ahead 2, behind 1]
            auto parseNum = [&f](const std::string& tag) -> int {
                size_t p = f[3].find(tag);
                if (p == std::string::npos) return 0;
                p += tag.size();
                int v = 0;
                while (p < f[3].size() && isdigit((unsigned char)f[3][p]))
                    v = v * 10 + (f[3][p++] - '0');
                return v;
            };
            b.ahead  = parseNum("ahead ");
            b.behind = parseNum("behind ");
        }
        result.value.push_back(b);
    }
    return result;
}

CommandResult GitClient::checkout(const std::string& branchName,
                                  bool createIfMissing) const {
    if (createIfMissing)
        return runGit({"checkout", "-b", branchName});
    return runGit({"checkout", branchName});
}

CommandResult GitClient::createBranch(const std::string& name,
                                      const std::string& startPoint) const {
    if (startPoint.empty()) return runGit({"branch", name});
    return runGit({"branch", name, startPoint});
}

CommandResult GitClient::deleteBranch(const std::string& name,
                                      bool force) const {
    return runGit({"branch", force ? "-D" : "-d", name});
}

CommandResult GitClient::merge(const std::string& refName) const {
    return runGit({"merge", refName});
}

CommandResult GitClient::abortMerge() const { return runGit({"merge", "--abort"}); }

Result<std::vector<MergeConflict>> GitClient::conflicts() const {
    Result<std::vector<MergeConflict>> result;
    auto st = status();
    if (!st.ok()) { result.raw = st.raw; return result; }

    for (auto& fs : st.value) {
        if (!fs.modified || !fs.staged) continue;
        // Read the file and extract <<<<<<< ======= >>>>>>> blocks.
        std::string full = repoPath_ + "/" + fs.path;
        FILE* fp = fopen(full.c_str(), "rb");
        if (!fp) continue;
        std::string content;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            content.append(buf, n);
        fclose(fp);

        MergeConflict mc;
        mc.path = fs.path;
        std::istringstream in(content);
        std::string line;
        int ln = 1;
        enum class Phase { None, Ours, Theirs } phase = Phase::None;
        ConflictHunk h;
        while (std::getline(in, line)) {
            if (line.rfind("<<<<<<<", 0) == 0) {
                phase = Phase::Ours; h = ConflictHunk{}; h.path = fs.path;
                h.beginLine = ln;
            } else if (line.rfind("=======", 0) == 0) {
                phase = Phase::Theirs;
            } else if (line.rfind(">>>>>>>", 0) == 0) {
                mc.hunks.push_back(h); phase = Phase::None;
            } else if (phase == Phase::Ours)   h.ours += line + "\n";
            else if (phase == Phase::Theirs)  h.theirs += line + "\n";
            ++ln;
        }
        if (!mc.hunks.empty()) result.value.push_back(mc);
    }
    return result;
}

CommandResult GitClient::resolveConflict(
    const std::string& path, const std::string& resolvedContent) const {
    std::string full = repoPath_ + "/" + path;
    FILE* fp = fopen(full.c_str(), "wb");
    if (!fp) return {-1, "cannot open " + path};
    fwrite(resolvedContent.data(), 1, resolvedContent.size(), fp);
    fclose(fp);
    return markConflictResolved(path);
}

CommandResult GitClient::markConflictResolved(const std::string& path) const {
    return stage({path});
}

Result<std::vector<StashEntry>> GitClient::stashList() const {
    auto r = runGit({"stash", "list", "--format=%gd%x00%gs"});
    Result<std::vector<StashEntry>> result{ {}, r };
    if (!r.ok()) return result;
    for (auto& line : splitLines(r.output)) {
        auto f = splitOnNul(line);
        if (f.size() < 2) continue;
        StashEntry s;
        s.label = f[1];
        if (s.label.rfind("stash@{",0)==0) {
            int idx = atoi(s.label.substr(7).c_str());
            s.index = idx;
        }
        result.value.push_back(s);
    }
    return result;
}

CommandResult GitClient::stashPush(const std::string& message) const {
    if (message.empty()) return runGit({"stash", "push"});
    return runGit({"stash", "push", "-m", message});
}

CommandResult GitClient::stashPop(int index) const {
    return runGit({"stash", "pop", "stash@{" + std::to_string(index) + "}"});
}

CommandResult GitClient::stashApply(int index) const {
    return runGit({"stash", "apply", "stash@{" + std::to_string(index) + "}"});
}

CommandResult GitClient::stashDrop(int index) const {
    return runGit({"stash", "drop", "stash@{" + std::to_string(index) + "}"});
}

Result<std::vector<RemoteInfo>> GitClient::remotes() const {
    auto names = runGit({"remote"});
    Result<std::vector<RemoteInfo>> result{ {}, names };
    if (!names.ok()) return result;
    for (auto& nm : splitLines(names.output)) {
        if (nm.empty()) continue;
        RemoteInfo ri;
        ri.name = trim(nm);
        auto urls = runGit({"remote", "get-url", ri.name});
        if (urls.ok()) ri.fetchUrl = trim(urls.output);
        ri.pushUrl = ri.fetchUrl;
        result.value.push_back(ri);
    }
    return result;
}

CommandResult GitClient::addRemote(const std::string& name,
                                   const std::string& url) const {
    return runGit({"remote", "add", name, url});
}

CommandResult GitClient::removeRemote(const std::string& name) const {
    return runGit({"remote", "remove", name});
}

CommandResult GitClient::fetch(const std::string& remoteName) const {
    return runGit({"fetch", remoteName, "--prune"});
}

CommandResult GitClient::pull(const std::string& remoteName,
                              const std::string& branchName) const {
    return runGit({"pull", remoteName, branchName});
}

CommandResult GitClient::push(const std::string& remoteName,
                              const std::string& branchName,
                              bool setUpstream) const {
    std::vector<std::string> args{"push"};
    if (setUpstream) args.push_back("-u");
    args.push_back(remoteName);
    args.push_back(branchName);
    return runGit(args);
}

Result<std::vector<BlameLine>> GitClient::blame(const std::string& path) const {
    auto r = runGit({"blame", "--porcelain", "--", path});
    Result<std::vector<BlameLine>> result{ {}, r };
    if (!r.ok()) return result;

    std::unordered_map<std::string, std::pair<std::string,std::string>> meta;
    BlameLine* cur = nullptr;
    for (auto& line : splitLines(r.output)) {
        if (line.size() >= 8 && !line.empty() &&
            (isdigit(line[0])||islower(line[0]))) {
            auto sp = line.find(' ');
            std::string hash = sp==std::string::npos?line:line.substr(0,sp);
            BlameLine b;
            b.hash = hash;
            result.value.push_back(b);
            cur = &result.value.back();
        } else if (cur && line.rfind("author ",0)==0) {
            cur->author = line.substr(7);
        } else if (cur && line.rfind("author-time ",0)==0) {
            long long t = atoll(line.c_str()+12);
            time_t tt = static_cast<time_t>(t);
            char dbuf[64];
            strftime(dbuf, sizeof(dbuf), "%Y-%m-%d %H:%M", localtime(&tt));
            cur->date = dbuf;
        }
    }
    int ln = 1;
    for (auto& b : result.value) b.lineNumber = ln++;
    // Fill content lazily via separate cat-file call per unique hash when needed;
    // for now leave content empty — UI shows author/date/line number.
    return result;
}

// ---- helpers -------------------------------------------------------------

std::vector<std::string> GitClient::splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::vector<std::string> GitClient::splitOnNul(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\0') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string GitClient::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace me::gitclient
