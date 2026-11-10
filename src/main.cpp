#include "c0/driver.h"
#include "c0/typecheck/typechecker.h"
#include <fstream>
#include <iostream>
#include <string>

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options] <file.c0>\n"
              << "Options:\n"
              << "  --dump-ast       Print AST to stdout upon successful parse\n"
              << "  --benchmark <N>  Run parsing and typechecking N times in-process for profiling\n"
              << "  -h, --help       Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string filepath;
    bool dump_ast = false;
    int benchmark_iters = 1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-ast") {
            dump_ast = true;
        } else if (arg == "--benchmark" && i + 1 < argc) {
            benchmark_iters = std::stoi(argv[++i]);
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

    if (benchmark_iters > 1) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open file: " << filepath << "\n";
            return 1;
        }
        std::string source((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        int exit_code = 0;
        for (int iter = 0; iter < benchmark_iters; ++iter) {
            c0::Driver driver;
            bool parse_ok = driver.parse_string(source, filepath) && !driver.has_errors();
            if (!parse_ok) {
                exit_code = 1;
                continue;
            }
            c0::TypeChecker type_checker(driver);
            if (!driver.ast() || !type_checker.check(*driver.ast())) {
                exit_code = 1;
                continue;
            }
            exit_code = 0;
        }
        return exit_code;
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

    c0::TypeChecker type_checker(driver);
    if (!driver.ast() || !type_checker.check(*driver.ast())) {
        driver.print_diagnostics(std::cerr);
        return 1;
    }

    if (dump_ast && driver.ast()) {
        driver.ast()->dump(std::cout);
    }

    return 0;
}
