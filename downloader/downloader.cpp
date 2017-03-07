/**

  Downloader now only downloads the JS files and stores them on disk. Does not attempt to tokenize them as 3rd party tokenizer will be used in the future to address parsing correctness.

  All pipeline should work w/o database and should also check histories of files. Could be easily-ish retargettable for different languages.

 */

#include <cstdlib>
#include <iostream>
#include <unordered_map>

#include "include/csv.h"
#include "include/filesystem.h"
#include "include/settings.h"
#include "include/exec.h"
#include "include/hash.h"
#include "include/pattern_lists.h"

#include "git.h"


#include "include/worker.h"



static_assert(sizeof(long) == 8, "We need a lot of ids");

class Project {
public:

    /** Creates new project pointing to the given git url.

      The project is assigned a new unique id.
     */
    Project(std::string const & gitUrl):
        id_(idIndex_++),
        gitUrl_(gitUrl) {
    }

    /** Creates a project with given url and sets its id to the provided value.

      Also updates the internal id counter so that newly created objects will have id greater than this one.
     */
    Project(std::string const & gitUrl, long id):
        id_(id),
        gitUrl_(gitUrl) {
        // make sure we update the counter, since there should be only one thread creating projects, I expect no contention and this is just defensive
        while (idIndex_ <= id) {
            long old = idIndex_;
            if (idIndex_.compare_exchange_weak(old, id + 1))
                break;
        }
    }

    /** Returns the id of the project.
     */
    long id() const {
        return id_;
    }

    /** Returns the git url of the project.
     */
    std::string const & gitUrl() const {
        return gitUrl_;
    }

    std::string const & localPath() const {
        return localPath_;
    }

    void setLocalPath(std::string const & path) {
        localPath_ = path;
    }


    /** Default constructor required by the worker framework.
     */
    Project():
        id_(-1) {
    }

    bool & hasDeniedFiles() {
        return hasDeniedFiles_;
    }

    bool hasDeniedFiles() const {
        return hasDeniedFiles_;
    }


private:
    friend std::ostream & operator << (std::ostream & s, Project const & task) {
        s << task.gitUrl() << " [" << task.id() << "]";
        return s;
    }


    /** Id of the project.
     */
    long id_;


    /** Git url from which the repo can be cloned.
     */
    std::string gitUrl_;

    std::string localPath_;

    /** True if there were some files that are explicitly denied.
     */
    bool hasDeniedFiles_;


    static std::atomic<long> idIndex_;
};


/**  File Snapshots.

  The downloader itself does not work with files, but file snapshots. Each file snapshot is identified by the following:

  - commit hash string
  - relative path of the file in the given commit

  Each snapshot has its own id number and and contains an index of its previous snapshot, as well as and index of the contents of the file at that particular commit.

 */

class FileSnapshot {
public:

    std::string const & commit() const {
        return commit_;
    }

    std::string const & relPath() const {
        return relPath_;
    }

    /** Creates a file snapshot from given git file history record.
     */
    FileSnapshot(Git::FileHistory const & h):
        id_(-1),
        commit_(h.hash),
        relPath_(h.filename),
        contentId_(-1),
        time_(h.date) {
    }

    long id() const {
        return id_;
    }

    long & id() {
        return id_;
    }

    long contentId() const {
        return contentId_;
    }

    long & contentId() {
        return contentId_;
    }

    bool operator == (FileSnapshot const & other) const {
        return commit_ == other.commit_ and relPath_ == other.relPath_;
    }

private:
    long id_;
    std::string commit_;
    std::string relPath_;
    long contentId_;
    int time_;
};

namespace std {

    template<>
    struct hash<::FileSnapshot> {

        std::size_t operator()(::FileSnapshot const & f) const {
            return std::hash<std::string>{}(f.commit()) + std::hash<std::string>{}(f.relPath());
        }

    };

}

/** Branch Snapshot

  While FileSnapshots provide information about all possible versions of files, the branch snapshot is used to determine which files were part of which branch at given time.

  For each branch visited, the downloader lists the file snapshot ids for the files present in that branch.

  Branch snapshots are stored in `branch_` prefixed files,

 */
class BranchSnapshot {
public:

};




/** Downloads git projects and their files.

  The downloader can either append new projects to existing output, or it can rescan projects it has already seen for any changes and only update these. All outputs from the downloader are stored in the OutputPath and have the following structure:

  temp - this is where the temporarily downloaded projects live, the temp directory is always deleted when downloader finishes

  projects - contains scanned projects (see below)

  data - contains unique file contents (see below)

  stats - contains statistics about the downloader session (see below)

  ### Projects

  To avoid straining the filesystem, project ID's are stored hierarchically in









 */
class Downloader : public Worker<Downloader, Project> {
public:

    static void Initialize(PatternList const & p) {

        filePattern_ = p;

        // for now, just make sure the directories exist
        createPathIfMissing(Settings::OutputPath);
        createPathIfMissing(TempPath());
        createPathIfMissing(StatsPath());
        createPathIfMissing(ProjectsPath());
        createPathIfMissing(FilesPath());
    }

    /** Reads the given file, and schedules each project in it for the download.

      The file should contain a git url per line.
     */
    static void FeedProjectsFrom(std::string const & filename) {
        CSVParser p(filename);
        unsigned line = 0;
        for (auto x : p) {
            ++line;
            if (x.size() == 1) {
                Schedule(Project(x[0]));
                continue;
            } else if (x.size() == 2) {
                try {
                    char ** c;
                    Schedule(Project(x[0], std::strtol(x[1].c_str(), c, 10)));
                    continue;
                } catch (...) {
                }
            }
            Error(STR(filename << ", line " << line << ": Invalid format of the project url input, skipping."));
        }
    }

private:
    /** For each project, the dowloader does the following:

      - attempt to clone the project, if this fails, the project's url is reported to the failed files

     */
    void run(Project & task) override {
        Log(STR("Processing task " << task));
        // clone the project
        download(task);
        // process all branches
        processAllBranches(task);





        // all work is done, delete the project
        deleteProject(task);
    }


    void download(Project & p) {
        p.setLocalPath(STR(TempPath() << "/" << p.id()));
        // if by chance the dir already exists (from last execution, remove it so that we can clone into)
        if (isDirectory(p.localPath()))
            deletePath(p.localPath());
        if (not Git::Clone(p.gitUrl(), p.localPath())) {
            // the project can't be downloaded, output it to the failed list
            // TODO
            throw std::runtime_error(STR("Unable to download project " << p.gitUrl()));
        }
        Log(STR(p << " successfully cloned to local path " << p.localPath()));
    }

    void processAllBranches(Project & p) {
        std::unordered_set<std::string> branches = Git::GetBranches(p.localPath());
        std::string current = Git::GetCurrentBranch(p.localPath());
        while (true) {
            branches.erase(current); // remove current branch
            Log(STR("Analyzing branch " << current));
            // process all files we can find in the branch
            processFiles(p, current);
            // move to the next branch
            while (true) {
                if (branches.empty())
                    return; // we are done
                // otherwise get new branch, remove from the list
                current = * branches.begin();
                branches.erase(branches.begin());
                // and checkout
                if (not Git::SetBranch(p.localPath(), current)) {
                    Error(STR("Unable to checkout branch " << current));
                    continue;
                }
                break;
            }
        }
    }

    /** Processes files in the current branch.

     */
    void processFiles(Project & p, std::string const & branchName) {
        // get all files reported in the branch
        for (Git::FileInfo const & file : Git::GetFileInfo(p.localPath())) {
            bool denied;
            if (filePattern_.check(file.filename, denied)) {
                // TODO if the file exists, add it to the branch information
                //if (isFile(STR(p.localPath() << "/" << file.filename)))
                //    throw "NOT IMPLEMENTED";
                // get the file history and create the snapshots where missing
                // TODO reverse!!!!!
                for (Git::FileHistory const & fh : Git::GetFileHistory(p.localPath(), file)) {
                    FileSnapshot fs(fh);
                    // if the file snapshot does not yet exist, we must add it, get the contents and add the contents to the
                    if (files_.find(fs) == files_.end()) {
                        // get the source
                        std::string text;
                        if (Git::GetFileRevision(p.localPath(), fh, text)) {
                            // assign the snapshot id
                            fs.id() = files_.size();
                            fs.contentId() = getContentId(text);
                            // add the file snapshot to current project's snapshots
                            files_.insert(fs);
                        }
                    }
                }
            } else if (denied) {
                p.hasDeniedFiles() = true;
            }
        }
    }

    long getContentId(std::string const & text) {
        // hash the file contents
        Hash h = Hash::Calculate(text);
        long id;
        {
            // TODO lock this
            auto i = fileHashes_.find(h);
            if (i != fileHashes_.end())
                return i->second;
            id = fileHashes_.size();
            fileHashes_.insert(std::pair<Hash, long>(h, id));
        }
        // now we need to store the file
        std::string targetDir = STR(FilesPath() << Settings::IdToPath(id));
        createPathIfMissing(targetDir);
        std::ofstream out(STR(targetDir << "/" << id << ".raw"));
        out << text;
        out.close();
        if (Settings::ClosesPathDir(id)) {
            // TODO compress the directory contents
            std::cout << "done target dir " << targetDir << std::endl;
        }
    }



    /** Just deletes the local path associated with the project.
     */
    void deleteProject(Project & p) {
        deletePath(p.localPath());
        Log(STR(p.localPath() << " deleted."));
    }





    static std::string TempPath() {
        return Settings::OutputPath + "/temp";
    }

    static std::string StatsPath() {
        return Settings::OutputPath + "/stats";
    }

    static std::string ProjectsPath() {
        return Settings::OutputPath + "/projects";
    }

    static std::string FilesPath() {
        return Settings::OutputPath + "/files";
    }








    /** File snapshots in the current project.
     */
    std::unordered_set<FileSnapshot> files_;


    /** Contains a map of all file hashes seen so far and their ids.
     */
    static std::unordered_map<Hash, long> fileHashes_;

    /** File patterns to accept, or deny.
     */
    static PatternList filePattern_;
};


std::unordered_map<Hash, long> Downloader::fileHashes_;

PatternList Downloader::filePattern_;






std::atomic<long> Project::idIndex_(0);








int main(int argc, char * argv[]) {


    Settings::OutputPath = "/data/ele";
    Downloader::Initialize(PatternList::JavaScript());
    Downloader::Spawn(1);
    Downloader::Run();
    Downloader::FeedProjectsFrom("/home/peta/devel/ele-pipeline/project_urls.csv");
    Downloader::Wait();
    std::cout << "haha" << std::endl;
    /*

    Downloader::Spawn(10);
    Downloader::Run();
    Downloader::Stop();


    std::cout << "Oh Hai!" << std::endl;

    std::string repoUrl = "/home/peta/devel/js-tokenizer";

    auto i = Git::getFileInfo(repoUrl);
    for (auto x : i) {
        std::cout << x.filename << " " << x.created << std::endl;
        //if (isFile(STR(repoUrl + "/" + x.filename))) {
            auto hist = Git::getFileHistory(repoUrl, x);
            for (auto hx : hist) {
                std::cout << "    " << hx.hash << " " << hx.date << " - " << hx.filename << ": ";
                std::string contents;
                if (Git::getFileRevision(repoUrl, hx, contents)) {
                    std::cout << contents.size() << std::endl;
                    //std::cout << contents;
                }
                else
                    std::cout << "DELETED";
                std::cout << std::endl;
            }
        //}
    } */

/*
    CSVParser p("/data/ghtorrent/projects.csv");
    unsigned x = 0;
    for (auto row : p) {
        for (std::string const & s : row) {
            std::cout << s << " ";
        }
        std::cout << std::endl;
        ++x;
    }
    std::cout << x << std::endl;

    */

    return EXIT_SUCCESS;
}
