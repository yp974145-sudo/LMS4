#include "lms/file_records.h"
#include <iostream>

int main(int argc,char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("Usage: lms-storage --check|--restore users|orders FILE");
        const std::string action(argv[1]),kind(argv[2]);
        if (action != "--check" && action != "--restore") throw std::invalid_argument("Unknown storage action");
        const auto file=std::filesystem::absolute(argv[3]);
        lms::file_storage::DirectoryLock lock(file.parent_path());
        if (action == "--check") {
            lms::file_records::check(file,kind);
            std::cout << "PASS: snapshot is structurally valid\n";
        } else {
            const auto damaged=lms::file_records::restore(file,kind);
            std::cout << "RESTORED previous snapshot; latest changes may be absent.\n";
            if (!damaged.empty()) std::cout << "Preserved previous main file: " << damaged.string() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Storage action failed: " << error.what() << '\n'; return 1;
    }
}
