#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <random>
#include <ctime>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <cstdlib>
#include <regex>
#include <iomanip>
#include <signal.h>
#include <sys/types.h>

namespace fs = std::filesystem;

// ======================
// Utils
// ======================

class Utils {
public:
    static std::string get_home_dir() {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home);
        return "/tmp";
    }

    static std::string format_time(time_t timestamp) {
        std::stringstream ss;
        ss << std::put_time(std::localtime(&timestamp), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    static bool file_exists(const std::string& path) {
        return fs::exists(path);
    }
};

// ======================
// Tag Struct
// ======================

struct Tag {
    std::string id;
    std::string type;
    std::string content;
    std::string file_path;
    std::string relative_path;
    int line_number;
    time_t last_modified;

    bool operator==(const Tag& other) const {
        return id == other.id;
    }
};

namespace std {
    template<> struct hash<Tag> {
        size_t operator()(const Tag& t) const {
            return hash<std::string>()(t.id);
        }
    };
}

// ======================
// TagDatabase
// ======================

class TagDatabase {
private:
    mutable std::mutex db_mutex;
    std::unordered_map<std::string, Tag> tags_by_id;               // id -> tag
    std::unordered_map<std::string, std::set<std::string>> file_to_ids; // file -> {ids}

public:
    void add_tag(const Tag& tag) {
        std::lock_guard<std::mutex> lock(db_mutex);
        tags_by_id[tag.id] = tag;
        file_to_ids[tag.file_path].insert(tag.id);
    }

    void remove_tag(const std::string& id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = tags_by_id.find(id);
        if (it != tags_by_id.end()) {
            file_to_ids[it->second.file_path].erase(id);
            if (file_to_ids[it->second.file_path].empty()) {
                file_to_ids.erase(it->second.file_path);
            }
            tags_by_id.erase(it);
        }
    }

    void remove_tags_in_file(const std::string& file_path) {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto file_it = file_to_ids.find(file_path);
        if (file_it != file_to_ids.end()) {
            for (const auto& id : file_it->second) {
                tags_by_id.erase(id);
            }
            file_to_ids.erase(file_it);
        }
    }

    void remove_tags_in_paths(const std::vector<std::string>& paths_to_remove) {
        std::lock_guard<std::mutex> lock(db_mutex);
        std::vector<std::string> ids_to_remove;
        for (const auto& [id, tag] : tags_by_id) {
            for (const auto& bad_path : paths_to_remove) {
                if (tag.file_path == bad_path || 
                    (tag.file_path.size() > bad_path.size() && 
                     tag.file_path.substr(0, bad_path.size()) == bad_path &&
                     tag.file_path[bad_path.size()] == '/')) {
                    ids_to_remove.push_back(id);
                    break;
                }
            }
        }
        for (const auto& id : ids_to_remove) {
            remove_tag(id);
        }
    }

    std::vector<Tag> get_all_tags() const {
        std::lock_guard<std::mutex> lock(db_mutex);
        std::vector<Tag> result;
        for (const auto& [_, tag] : tags_by_id) {
            result.push_back(tag);
        }
        return result;
    }

    std::set<std::string> get_tag_ids_in_file(const std::string& file_path) const {
        std::lock_guard<std::mutex> lock(db_mutex);
        auto it = file_to_ids.find(file_path);
        if (it != file_to_ids.end()) {
            return it->second;
        }
        return {};
    }
};

// ======================
// TagParser
// ======================

class TagParser {
private:
    const std::set<std::string> tag_types = {
        "NOTE", "TODO", "WARNING", "WARN", "FIXME", "FIX", "BUG"
    };

    std::string generate_id() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        std::string id = "CT-";
        for (int i = 0; i < 8; ++i) {
            id += "0123456789ABCDEF"[dis(gen)];
        }
        return id;
    }

    bool is_tag_line(const std::string& line, std::string& tag_type, std::string& content) const {
        for (const auto& type : tag_types) {
            std::string pattern = type + ":";
            size_t pos = line.find(pattern);
            if (pos != std::string::npos) {
                size_t double_slash = line.find("//");
                size_t slash_star = line.find("/*");
                size_t hash = line.find("#");
                if ((double_slash != std::string::npos && double_slash <= pos) ||
                    (slash_star != std::string::npos && slash_star <= pos) ||
                    (hash != std::string::npos && hash <= pos)) {
                    tag_type = type;
                    content = line.substr(pos + pattern.length());
                    content.erase(0, content.find_first_not_of(" \t"));
                    return true;
                }
            }
        }
        return false;
    }

    bool has_codetag_id(const std::string& line) const {
        size_t pos = 0;
        while ((pos = line.find("[CT-", pos)) != std::string::npos) {
            size_t end = line.find("]", pos);
            if (end != std::string::npos && end > pos) {
                std::string id_candidate = line.substr(pos, end - pos + 1);
                if (id_candidate.length() == 12 && id_candidate[10] == ']') {
                    bool valid = true;
                    for (size_t i = 4; i < 10; i++) {
                        char c = id_candidate[i];
                        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                            valid = false;
                            break;
                        }
                    }
                    if (valid) return true;
                }
            }
            pos++;
        }
        return false;
    }

    std::string extract_codetag_id(const std::string& line) const {
        size_t pos = 0;
        while ((pos = line.find("CT-", pos)) != std::string::npos) {
            if (pos + 11 <= line.length() && (pos == 0 || !std::isalnum(line[pos-1]))) {
                bool valid = true;
                for (size_t i = pos + 3; i < pos + 11; i++) {
                    char c = line[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                        valid = false;
                        break;
                    }
                }
                if (valid && (pos + 11 >= line.length() || !std::isalnum(line[pos + 11]))) {
                    return line.substr(pos, 11);  // "CT-" + 8 hex chars
                }
            }
            pos++;
        }
        return "";
    }

    void add_codetag_id(std::string& line, const std::string& id) const {
        if (!extract_codetag_id(line).empty()) {
            return;  // Don't add if an ID already exists
        }
        
        for (const auto& type : tag_types) {
            std::string pattern = type + ":";
            size_t pos = line.find(pattern);
            if (pos != std::string::npos) {
                size_t content_start = pos + pattern.length();
                line.insert(content_start, " " + id);
                break;
            }
        }
    }

    std::string get_file_relative_path(const std::string& file_path, const std::string& base_dir) const {
        if (file_path.length() >= base_dir.length() &&
            file_path.compare(0, base_dir.length(), base_dir) == 0) {
            return file_path.substr(base_dir.length() + 1);
        }
        return file_path;
    }

public:
    std::vector<Tag> parse_file(const std::string& file_path, const std::string& base_dir, time_t mtime) {
        std::vector<Tag> tags;
        std::ifstream file(file_path);
        if (!file.is_open()) return tags;

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) lines.push_back(line);
        file.close();

        bool modified = false;
        for (auto& current_line : lines) {
            std::string tag_type, content;
            if (is_tag_line(current_line, tag_type, content) && !has_codetag_id(current_line)) {
                std::string new_id = generate_id();
                add_codetag_id(current_line, new_id);
                modified = true;
            }
        }

        if (modified) {
            std::ofstream out(file_path);
            for (const auto& l : lines) out << l << "\n";
            out.close();
            struct stat st;
            if (stat(file_path.c_str(), &st) == 0) mtime = st.st_mtime;
        }

        int line_number = 0;
        for (const auto& current_line : lines) {
            line_number++;
            std::string tag_type, content;
            if (is_tag_line(current_line, tag_type, content)) {
                Tag tag;
                tag.type = tag_type;
                tag.file_path = file_path;
                tag.relative_path = get_file_relative_path(file_path, base_dir);
                tag.line_number = line_number;
                tag.last_modified = mtime;

                std::string existing_id = extract_codetag_id(current_line);
                if (!existing_id.empty()) {
                    tag.id = existing_id;
                    
                    size_t tag_pos = current_line.find(tag_type + ":");
                    if (tag_pos != std::string::npos) {
                        size_t content_start = tag_pos + tag_type.length() + 1;
                        std::string raw_content = current_line.substr(content_start);
                        raw_content.erase(0, raw_content.find_first_not_of(" \t"));
                        
                        size_t id_pos = raw_content.find(existing_id);
                        if (id_pos != std::string::npos) {
                            tag.content = raw_content.substr(0, id_pos) + raw_content.substr(id_pos + existing_id.length());
                            tag.content.erase(0, tag.content.find_first_not_of(" \t"));
                        } else {
                            tag.content = raw_content;
                        }
                    } else {
                        tag.content = content;
                    }
                } else {
                    tag.id = generate_id();
                    tag.content = content;
                }
                
                tags.push_back(tag);
            }
        }
        return tags;
    }

    bool is_source_file(const std::string& ext) const {
        return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" ||
               ext == ".java" || ext == ".js" || ext == ".ts" || ext == ".py" ||
               ext == ".rb" || ext == ".go" || ext == ".rs" || ext == ".php";
    }
};

// ======================
// FileWatcher
// ======================

class FileWatcher {
private:
    std::string directory_path;
    std::string ignore_file_path;
    std::atomic<bool> running{false};
    std::thread watcher_thread;
    int inotify_fd{-1};
    mutable std::mutex ignore_patterns_mutex;
    std::vector<std::string> ignore_patterns;
    std::string codetags_file;
    std::shared_ptr<TagDatabase> tag_db;  // Each repo has its own database

    std::unordered_map<std::string, time_t> last_known_mtime;
    std::unordered_set<std::string> currently_ignored_files;
    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;

    void load_ignore_patterns() {
        std::lock_guard<std::mutex> lock(ignore_patterns_mutex);
        ignore_patterns.clear();
        if (!Utils::file_exists(ignore_file_path)) return;
        std::ifstream file(ignore_file_path);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line[0] != '#' && line[0] != ' ') {
                ignore_patterns.push_back(line);
            }
        }
    }

    bool should_ignore(const std::string& path) const {
        std::lock_guard<std::mutex> lock(ignore_patterns_mutex);
        
        std::string rel_path;
        if (path.length() > directory_path.length() && 
            path.substr(0, directory_path.length()) == directory_path &&
            path[directory_path.length()] == '/') {
            rel_path = path.substr(directory_path.length() + 1);
        } else if (path == directory_path) {
            return false;
        } else {
            rel_path = path;
        }
        
        if (rel_path.empty()) return false;
        
        for (const auto& pattern : ignore_patterns) {
            if (pattern.empty() || pattern[0] == '#') continue;
            
            std::string actual_pattern = pattern;
            bool match_dirs_only = false;
            
            if (!actual_pattern.empty() && actual_pattern.back() == '/') {
                match_dirs_only = true;
                actual_pattern.pop_back();
            }
            
            bool anchored = false;
            if (!actual_pattern.empty() && actual_pattern[0] == '/') {
                anchored = true;
                actual_pattern = actual_pattern.substr(1);
            }
            
            std::vector<std::string> paths_to_check;
            paths_to_check.push_back(rel_path);
            
            std::string temp = rel_path;
            while (!temp.empty()) {
                size_t last_slash = temp.find_last_of('/');
                if (last_slash == std::string::npos) break;
                temp = temp.substr(0, last_slash);
                paths_to_check.push_back(temp + "/");
            }
            
            for (const auto& check_path : paths_to_check) {
                std::string clean_check = check_path;
                if (clean_check.back() == '/') clean_check.pop_back();
                
                if (fnmatch(actual_pattern.c_str(), clean_check.c_str(), FNM_PATHNAME) == 0) {
                    if (match_dirs_only && check_path.back() != '/') {
                        continue;
                    }
                    return true;
                }
                
                if (!anchored) {
                    std::string wildcard_pattern = "*" + actual_pattern;
                    if (fnmatch(wildcard_pattern.c_str(), clean_check.c_str(), FNM_PATHNAME) == 0) {
                        if (match_dirs_only && check_path.back() != '/') {
                            continue;
                        }
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void update_codetags_file() {
        auto all_tags = tag_db->get_all_tags();  // Get only this repo's tags
        try {
            std::ofstream file(codetags_file);
            file << "# Codetags\n";
            std::map<std::string, std::vector<Tag>> grouped;
            for (const auto& tag : all_tags) grouped[tag.type].push_back(tag);
            for (const auto& [type, vec] : grouped) {
                file << "## " << type << "\n";
                for (const auto& tag : vec) {
                    file << "- **[" << tag.id << "]** " << tag.content << "\n";
                    file << "  - *File:* " << tag.relative_path << ":" << tag.line_number << "\n";
                    file << "  - *Modified:* " << Utils::format_time(tag.last_modified) << "\n";
                }
            }
        } catch (...) {}
    }

    void process_file_event(const std::string& filepath) {
        if (should_ignore(filepath)) {
            tag_db->remove_tags_in_file(filepath);
            update_codetags_file();
            last_known_mtime.erase(filepath);
            return;
        }

        fs::path p(filepath);
        if (!p.has_extension()) {
            return;
        }
        std::string ext = p.extension().string();
        TagParser parser;
        if (!parser.is_source_file(ext)) {
             return;
        }

        struct stat st;
        if (stat(filepath.c_str(), &st) != 0) {
            tag_db->remove_tags_in_file(filepath);
            update_codetags_file();
            last_known_mtime.erase(filepath);
            return;
        }

        auto it = last_known_mtime.find(filepath);
        if (it != last_known_mtime.end() && it->second == st.st_mtime) {
            return;
        }

        last_known_mtime[filepath] = st.st_mtime;

        auto new_tags = parser.parse_file(filepath, directory_path, st.st_mtime);
        auto old_ids = tag_db->get_tag_ids_in_file(filepath);

        for (const auto& id : old_ids) {
            tag_db->remove_tag(id);
        }

        for (const auto& tag : new_tags) {
            tag_db->add_tag(tag);
        }

        update_codetags_file();
    }

    void process_ignore_file_change() {
        load_ignore_patterns();
        
        std::unordered_set<std::string> new_ignored_files;
        std::vector<std::string> files_to_process;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory_path)) {
                if (entry.is_regular_file()) {
                    std::string filepath = entry.path().string();
                    if (should_ignore(filepath)) {
                        new_ignored_files.insert(filepath);
                    } else {
                        files_to_process.push_back(filepath);
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            return;
        }
        
        // Find files that are newly ignored
        for (const auto& filepath : new_ignored_files) {
            if (currently_ignored_files.find(filepath) == currently_ignored_files.end()) {
                // Newly ignored - remove tags
                tag_db->remove_tags_in_file(filepath);
                last_known_mtime.erase(filepath);
            }
        }
        
        // Find files that are no longer ignored
        for (const auto& filepath : currently_ignored_files) {
            if (new_ignored_files.find(filepath) == new_ignored_files.end()) {
                // No longer ignored - add tags back by processing the file
                process_file_event(filepath);
            }
        }
        
        // Process all non-ignored files to make sure they're up to date
        for (const auto& filepath : files_to_process) {
            // Only process if not already handled above
            if (currently_ignored_files.count(filepath) == 0) {
                process_file_event(filepath);
            }
        }
        
        // Update our tracking set
        currently_ignored_files = std::move(new_ignored_files);
        update_codetags_file();
    }



    void add_watch_recursive(const std::string& path) {
        int wd = inotify_add_watch(inotify_fd, path.c_str(),
                                   IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
        
        if (wd < 0) {
            return;
        }
        
        wd_to_path[wd] = path;
        path_to_wd[path] = wd;

        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.is_directory()) {
                    add_watch_recursive(entry.path().string());
                }
            }
        } catch (const fs::filesystem_error& e) {
        }
    }

public:
    FileWatcher(const std::string& dir_path, std::shared_ptr<TagDatabase> db)
        : directory_path(dir_path),
          ignore_file_path(dir_path + "/.ctagsignore"),
          codetags_file(dir_path + "/codetags.md"),
          tag_db(db) {
        load_ignore_patterns();
    }  

    ~FileWatcher() {
        stop();
    }

    void start() {
        if (running) return;
        running = true;

        inotify_fd = inotify_init1(IN_NONBLOCK);
        if (inotify_fd < 0) {
            std::cerr << "[FileWatcher] Failed to initialize inotify." << std::endl;
            running = false;
            return;
        }

        add_watch_recursive(directory_path);
        
        if (Utils::file_exists(ignore_file_path)) {
            inotify_add_watch(inotify_fd, ignore_file_path.c_str(), IN_MODIFY);
        }

        watcher_thread = std::thread([this]() {
            char buffer[32768];
            fd_set read_fds;
            while (running) {
                FD_ZERO(&read_fds);
                FD_SET(inotify_fd, &read_fds);
                struct timeval timeout{1, 0};
                if (select(inotify_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) continue;

                ssize_t len = read(inotify_fd, buffer, sizeof(buffer) - 1);
                if (len <= 0) continue;

                for (ssize_t i = 0; i < len;) {
                    auto* event = reinterpret_cast<inotify_event*>(&buffer[i]);
                    
                    std::string dir_path = "";
                    auto wd_it = wd_to_path.find(event->wd);
                    if (wd_it != wd_to_path.end()) {
                        dir_path = wd_it->second;
                    }
                    
                    std::string full_path = "";
                    if (event->len > 0 && !dir_path.empty()) {
                        full_path = dir_path + "/" + event->name;
                    } else if (!dir_path.empty()) {
                        full_path = dir_path;
                    }

                    if (!dir_path.empty() && dir_path == directory_path && 
                        event->len > 0 && std::string(event->name) == ".ctagsignore") {
                        process_ignore_file_change();
                    } 
                    else if (event->mask & (IN_CREATE | IN_MOVED_TO) && (event->mask & IN_ISDIR)) {
                        if (!full_path.empty()) {
                            add_watch_recursive(full_path);
                        }
                    } 
                    else if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                        if (!full_path.empty()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            process_file_event(full_path);
                        }
                    } 
                    else if (event->mask & IN_MODIFY) {
                        if (!full_path.empty()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            process_file_event(full_path);
                        }
                    } 
                    else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                        if (!full_path.empty()) {
                            process_file_event(full_path);
                        }
                    }
                    
                    i += sizeof(inotify_event) + event->len;
                }
            }
            close(inotify_fd);
        });

        {
            std::lock_guard<std::mutex> lock(ignore_patterns_mutex);
            last_known_mtime.clear();
        }
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory_path)) {
                if (entry.is_regular_file()) {
                    std::string fp = entry.path().string();
                    if (!should_ignore(fp)) {
                        process_file_event(fp);
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[FileWatcher] Filesystem error during initial scan: " << e.what() << std::endl;
        }
    }

    void stop() {
        if (!running) return;
        running = false;
        
        for (const auto& [wd, path] : wd_to_path) {
            inotify_rm_watch(inotify_fd, wd);
        }
        wd_to_path.clear();
        path_to_wd.clear();
        
        if (watcher_thread.joinable()) watcher_thread.join();
        if (inotify_fd >= 0) {
            close(inotify_fd);
            inotify_fd = -1;
        }
    }
};

// ======================
// CodetagsDaemon
// ======================

struct Repository {
    std::string name;
    std::string path;
};

class CodetagsDaemon {
private:
    std::atomic<bool> running{true};
    std::string config_dir;
    std::string registered_repos_file;
    std::unordered_map<std::string, Repository> monitored_repos;
    std::unordered_map<std::string, std::unique_ptr<FileWatcher>> repo_watchers;
    std::unordered_map<std::string, std::shared_ptr<TagDatabase>> repo_databases; // Per-repo databases
    std::mutex repos_mutex;
    std::thread file_watcher;
    std::string daemon_pid_file;

public:
    CodetagsDaemon() {
        config_dir = Utils::get_home_dir() + "/.ctags";
        registered_repos_file = config_dir + "/registered_repos.txt";
        daemon_pid_file = config_dir + "/daemon.pid";
        fs::create_directories(config_dir);
        if (!fs::exists(registered_repos_file)) {
            std::ofstream f(registered_repos_file);
        }
    }

    ~CodetagsDaemon() {
        stop();
    }

    void load_and_watch_repos() {
        std::lock_guard<std::mutex> lock(repos_mutex);

        // Load all registered repos
        std::unordered_map<std::string, Repository> new_repos;
        std::ifstream file(registered_repos_file);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            size_t pos = line.find(':');
            if (pos != std::string::npos) {
                std::string name = line.substr(0, pos);
                std::string path = line.substr(pos + 1);
                if (fs::exists(path)) {
                    new_repos[name] = {name, path};
                }
            }
        }

        // Stop and remove watchers for repos that are no longer registered
        std::vector<std::string> to_remove;
        for (const auto& [name, _] : monitored_repos) {
            if (new_repos.find(name) == new_repos.end()) {
                to_remove.push_back(name);
            }
        }

        for (const auto& name : to_remove) {
            if (repo_watchers.find(name) != repo_watchers.end()) {
                repo_watchers[name]->stop();
                repo_watchers.erase(name);
            }
            repo_databases.erase(name);  // Remove database for unregistered repo
            monitored_repos.erase(name);
        }

        // Add or update watchers for registered repos
        for (const auto& [name, repo] : new_repos) {
            if (monitored_repos.find(name) == monitored_repos.end()) {
                // Create a new database for this repo
                auto repo_db = std::make_shared<TagDatabase>();
                repo_databases[name] = repo_db;
                
                repo_watchers[name] = std::make_unique<FileWatcher>(repo.path, repo_db);
                repo_watchers[name]->start();
                monitored_repos[name] = repo;
            }
        }
    }

    void run() {
        std::ofstream pid_file(daemon_pid_file);
        if (pid_file.is_open()) {
            pid_file << getpid() << std::endl;
            pid_file.close();
        }

        load_and_watch_repos();

        int inotify_fd = inotify_init1(IN_NONBLOCK);
        int wd = inotify_add_watch(inotify_fd, registered_repos_file.c_str(), IN_MODIFY);
        if (wd < 0) {
            close(inotify_fd);
            return;
        }

        file_watcher = std::thread([this, inotify_fd, wd]() {
            char buffer[4096];
            fd_set read_fds;
            while (running) {
                FD_ZERO(&read_fds);
                FD_SET(inotify_fd, &read_fds);
                struct timeval timeout{1, 0};
                if (select(inotify_fd + 1, &read_fds, nullptr, nullptr, &timeout) > 0) {
                    if (read(inotify_fd, buffer, sizeof(buffer)) > 0) {
                        load_and_watch_repos();
                    }
                }
            }
            inotify_rm_watch(inotify_fd, wd);
            close(inotify_fd);
        });

        while (running) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void stop() {
        running = false;
        if (file_watcher.joinable()) file_watcher.join();
        std::lock_guard<std::mutex> lock(repos_mutex);
        for (auto& [_, w] : repo_watchers) w->stop();

        if (fs::exists(daemon_pid_file)) {
            fs::remove(daemon_pid_file);
        }
    }
};

// ======================
// CodetagsApp
// ======================

class CodetagsApp {
private:
    std::string config_dir;
    std::string registered_repos_file;

    void kill_existing_daemon() {
        std::string daemon_pid_file = config_dir + "/daemon.pid";
        if (!fs::exists(daemon_pid_file)) return;

        std::ifstream f(daemon_pid_file);
        pid_t pid;
        if (f >> pid) {
            if (kill(pid, 0) == 0) {
                kill(pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (kill(pid, 0) == 0) {
                    kill(pid, SIGKILL);
                }
            }
        }
        fs::remove(daemon_pid_file);
    }

public:
    CodetagsApp() {
        config_dir = Utils::get_home_dir() + "/.ctags";
        registered_repos_file = config_dir + "/registered_repos.txt";
        fs::create_directories(config_dir);
    }

    void init() {
        kill_existing_daemon();

        auto repo_path = fs::current_path().string();
        auto repo_name = fs::path(repo_path).filename().string();

        std::ofstream md(repo_path + "/codetags.md");
        md << "# Codetags\n";
        md.close();

        // Check if repo is already registered
        std::ifstream in(registered_repos_file);
        std::string line;
        bool already_registered = false;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                size_t pos = line.find(':');
                if (pos != std::string::npos && line.substr(0, pos) == repo_name) {
                    already_registered = true;
                    break;
                }
            }
        }
        in.close();

        if (!already_registered) {
            std::ofstream reg(registered_repos_file, std::ios::app);
            reg << repo_name << ":" << repo_path << "\n";
            reg.close();
        }

        std::cout << "Codetags initialized in " << repo_path << ". Starting daemon in background...\n";
        
        std::thread daemon_thread([]() {
            CodetagsDaemon daemon;
            daemon.run();
        });
        daemon_thread.detach();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void remove() {
        auto repo_path = fs::current_path().string();
        auto repo_name = fs::path(repo_path).filename().string();

        std::ifstream in(registered_repos_file);
        std::ofstream out(registered_repos_file + ".tmp");
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                size_t pos = line.find(':');
                if (pos == std::string::npos || line.substr(0, pos) != repo_name) {
                    out << line << "\n";
                }
            }
        }
        in.close();
        out.close();
        fs::rename(registered_repos_file + ".tmp", registered_repos_file);

        fs::remove(repo_path + "/codetags.md");

        std::cout << "Repository removed from monitoring\n";
    }

    void scan_current() {
        auto repo_path = fs::current_path().string();
        auto db = std::make_shared<TagDatabase>();
        FileWatcher watcher(repo_path, db);
        watcher.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        watcher.stop();
        std::cout << "Manual scan completed.\n";
    }

    void run_daemon() {
        CodetagsDaemon daemon;
        daemon.run();
    }
};

// ======================
// main
// ======================

int main(int argc, char* argv[]) {
    CodetagsApp app;
    if (argc < 2) {
        std::cout << "Usage: codetags <command>\n";
        std::cout << "Commands:\n";
        std::cout << "  init     - Initialize codetags in current directory\n";
        std::cout << "  remove   - Remove current directory from monitoring\n";
        std::cout << "  scan     - Scan current directory for tags\n";
        std::cout << "  daemon   - Run the background daemon\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "init") app.init();
    else if (cmd == "remove") app.remove();
    else if (cmd == "scan") app.scan_current();
    else if (cmd == "daemon") app.run_daemon();
    else {
        std::cerr << "Unknown command: " << cmd << "\n";
        return 1;
    }
    return 0;
}
