#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage() {
    std::cerr << "Usage:\n";
    std::cerr << "  anvil-tools token-dump <file>\n";
    std::cerr << "  anvil-tools ast-dump <file>\n";
    std::cerr << "  anvil-tools asm-preview <file>\n";
    std::cerr << "  anvil-tools smoke-test\n";
}

static fs::path find_repo_root(const fs::path &argv0) {
    const std::vector<fs::path> starts = {
        fs::current_path(),
        fs::absolute(argv0).parent_path(),
    };

    for (const auto &start : starts) {
        fs::path cur = start;
        for (int i = 0; i < 6 && !cur.empty(); ++i) {
            if (fs::exists(cur / "anvil" / "bin" / "anv.exe"))
                return cur;
            cur = cur.parent_path();
        }
    }
    return {};
}

static fs::path find_anv_exe(const fs::path &repo_root) {
    fs::path anv = repo_root / "anvil" / "bin" / "anv.exe";
    if (fs::exists(anv))
        return anv;
    return {};
}

static int run_command(const std::string &cmd) {
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        return rc;
    return 0;
}

static std::string quote(const fs::path &p) {
    std::string s = p.string();
    return "\"" + s + "\"";
}

static int token_dump(const fs::path &anv, const fs::path &file) {
    return run_command(quote(anv) + " " + quote(file) + " -dump-tokens");
}

static int ast_dump(const fs::path &anv, const fs::path &file) {
    return run_command(quote(anv) + " " + quote(file) + " -dump-ast");
}

static int asm_preview(const fs::path &anv, const fs::path &file) {
    fs::path temp = fs::temp_directory_path() / "anv-preview.asm";
    int rc = run_command(quote(anv) + " " + quote(file) + " -asm -o " + quote(temp));
    if (rc != 0)
        return rc;

    std::ifstream in(temp);
    if (!in) {
        std::cerr << "asm-preview: cannot open " << temp.string() << "\n";
        return 1;
    }

    std::cout << in.rdbuf();
    std::error_code ec;
    fs::remove(temp, ec);
    return 0;
}

static bool has_source_ext(const fs::path &p) {
    auto ext = p.extension().string();
    return ext == ".hlx" || ext == ".c";
}

static int smoke_test(const fs::path &repo_root, const fs::path &anv) {
    const std::vector<fs::path> roots = {
        repo_root / "anvil" / "helix" / "test",
        repo_root / "anvil" / "c" / "test",
    };

    int failures = 0;
    int total = 0;

    for (const auto &root : roots) {
        if (!fs::exists(root))
            continue;
        for (const auto &entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || !has_source_ext(entry.path()))
                continue;
            ++total;
            std::string cmd = quote(anv) + " " + quote(entry.path()) + " -asm";
            int rc = run_command(cmd);
            if (rc != 0) {
                ++failures;
                std::cerr << "FAIL " << entry.path().string() << "\n";
            } else {
                std::cout << "OK " << entry.path().string() << "\n";
            }
        }
    }

    std::cout << "Smoke test: " << (total - failures) << "/" << total << " passed\n";
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    fs::path repo_root = find_repo_root(argv[0]);
    fs::path anv = repo_root.empty() ? fs::path{} : find_anv_exe(repo_root);
    if (anv.empty()) {
        std::cerr << "anvil-tools: cannot find anv.exe\n";
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "token-dump") {
        if (argc < 3) {
            usage();
            return 1;
        }
        return token_dump(anv, argv[2]);
    }
    if (cmd == "ast-dump") {
        if (argc < 3) {
            usage();
            return 1;
        }
        return ast_dump(anv, argv[2]);
    }
    if (cmd == "asm-preview") {
        if (argc < 3) {
            usage();
            return 1;
        }
        return asm_preview(anv, argv[2]);
    }
    if (cmd == "smoke-test")
        return smoke_test(repo_root, anv);

    usage();
    return 1;
}
