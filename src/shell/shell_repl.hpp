#pragma once

#include <string>
#include <vector>

namespace eugraph {
namespace shell {

// ==================== Value formatting ====================

// Format query results as an ASCII table
std::string formatTable(const std::vector<std::string>& columns, const std::vector<std::vector<std::string>>& rows);

// Format DDL result
std::string formatLabelCreated(const std::string& name, int id);
std::string formatEdgeLabelCreated(const std::string& name, int id);

// Format label list as a table
std::string formatLabelList(const std::vector<std::tuple<int, std::string, std::string>>& labels);
std::string formatEdgeLabelList(const std::vector<std::tuple<int, std::string, std::string, bool>>& labels);

// ==================== REPL ====================

struct ShellConfig {
    std::string host = "127.0.0.1";
    int port = 9090;
    std::string data_dir; // if non-empty, run in embedded mode (no server needed)
};

// Run the interactive REPL. Blocks until :exit.
void runRepl(const ShellConfig& config);

// Parse a shell command. Returns (command_name, args).
// If the line doesn't start with ':', returns ("", line) — a Cypher query.
std::pair<std::string, std::string> parseCommand(const std::string& line);

// Check if line ends with ';' (completes a Cypher statement)
bool isCompleteCypher(const std::string& input);

} // namespace shell
} // namespace eugraph
