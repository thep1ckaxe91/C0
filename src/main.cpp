#include "c0/driver.h"
#include <iostream>
#include <string>
#include <vector>

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options] <file.c0>\n"
              << "Options:\n"
              << "  --dump-ast   Print AST to stdout upon successful parse\n"
              << "  -h, --help   Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string filepath;
    bool dump_ast = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-ast") {
            dump_ast = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        } else {
            filepath = arg;
        }
    }

    if (filepath.empty()) {
        std::cerr << "Error: No input file specified.\n";
        print_usage(argv[0]);
        return 1;
    }

    c0::Driver driver;
    bool success = driver.parse_file(filepath);

    if (driver.has_errors()) {
        driver.print_diagnostics(std::cerr);
        return 1;
    }

    if (!success) {
        std::cerr << "Parsing failed without specific diagnostics.\n";
        return 1;
    }

    if (dump_ast && driver.ast()) {
        driver.ast()->dump(std::cout);
    }

    return 0;
}
