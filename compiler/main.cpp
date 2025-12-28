#include "lexer.h"
#include "parser.h"
#include "codegen.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <unordered_set>

namespace atom {

static std::string read_file(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static Program parse_with_imports(const std::string &path, std::unordered_set<std::string> &visited) {
    if (visited.count(path)) {
        return Program{};
    }
    visited.insert(path);
    std::string source = read_file(path);
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    Program program = parser.parse_program();

    Program merged;
    for (auto &imp : program.imports) {
        if (!imp.path.empty()) {
            std::string import_path = imp.path;
            if (import_path.find("/") == std::string::npos) {
                auto base = path.substr(0, path.find_last_of('/'));
                import_path = base + "/" + import_path;
            }
            Program child = parse_with_imports(import_path, visited);
            merged.structs.insert(
                merged.structs.end(),
                std::make_move_iterator(child.structs.begin()),
                std::make_move_iterator(child.structs.end()));
            merged.functions.insert(
                merged.functions.end(),
                std::make_move_iterator(child.functions.begin()),
                std::make_move_iterator(child.functions.end()));
        }
    }
    merged.structs.insert(
        merged.structs.end(),
        std::make_move_iterator(program.structs.begin()),
        std::make_move_iterator(program.structs.end()));
    merged.functions.insert(
        merged.functions.end(),
        std::make_move_iterator(program.functions.begin()),
        std::make_move_iterator(program.functions.end()));
    return merged;
}

} // namespace atom

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: atomc <input.atom> -o <output.wat>\n";
        return 1;
    }
    std::string input = argv[1];
    std::string output = "./output.wat";
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            output = argv[i + 1];
            i++;
        }
    }
    if (output.empty()) {
        std::cerr << "Output file not specified. Use -o <file>\n";
        return 1;
    }

    try {
        std::unordered_set<std::string> visited;
        atom::Program program = atom::parse_with_imports(input, visited);
        atom::CodegenResult result = atom::generate_wat(program);
        std::ofstream out(output);
        out << result.wat;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
