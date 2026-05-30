// Step definitions for openCypher TCK
#include "tck_context.hpp"

#include <cucumber_cpp/library/Context.hpp>
#include <cucumber_cpp/library/Hooks.hpp>
#include <cucumber_cpp/library/Steps.hpp>

#include <folly/Singleton.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

using namespace eugraph::tck;

// -----------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------

namespace {

std::unique_ptr<TckContext> gCtx;
std::atomic<int> gScenarioNum{0};

// Track scenario results for summary
std::vector<std::string> gPassedScenarios;
std::vector<std::string> gFailedScenarios;
std::vector<std::string> gSkippedScenarios;

// Step-level tracking
struct StepRecord {
    std::string scenario_name;
    int step_index;
    std::string step_text;
    std::string status; // "PASSED" or "FAILED"
};
std::vector<StepRecord> gStepRecords;
std::string gCurrentStepText;
std::vector<std::pair<std::string, std::string>> gPendingSteps; // (step_text, status)
int gStepIndex = 0;
bool gHadFailureBeforeStep = false;

void resetCtx() {
    gCtx = std::make_unique<TckContext>();
    gCtx->graphName = "tck_s" + std::to_string(++gScenarioNum);
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool skipIfUnsupported(const std::string& query) {
    if (!TckContext::isQuerySupported(query)) {
        return true;
    }
    return false;
}

// Parse top-level elements from a list string like "[a, [b, c], d]"
// Respects nested brackets and parentheses.
std::vector<std::string> parseTopLevelElements(const std::string& s) {
    std::vector<std::string> elems;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '[' || c == '(' || c == '{')
            ++depth;
        else if (c == ']' || c == ')' || c == '}')
            --depth;
        else if (c == ',' && depth == 0) {
            std::string elem = s.substr(start, i - start);
            // trim
            size_t a = elem.find_first_not_of(' ');
            size_t b = elem.find_last_not_of(' ');
            if (a != std::string::npos)
                elems.push_back(elem.substr(a, b - a + 1));
            start = i + 1;
        }
    }
    // last element
    std::string elem = s.substr(start);
    size_t a = elem.find_first_not_of(' ');
    size_t b = elem.find_last_not_of(' ');
    if (a != std::string::npos)
        elems.push_back(elem.substr(a, b - a + 1));
    return elems;
}

// Normalize a cell value: if it's a top-level list, sort its elements.
std::string normalizeListOrder(const std::string& cell) {
    if (cell.size() < 2 || cell.front() != '[' || cell.back() != ']')
        return cell;
    // Check for empty list
    std::string inner = cell.substr(1, cell.size() - 2);
    size_t firstNonSpace = inner.find_first_not_of(' ');
    if (firstNonSpace == std::string::npos)
        return cell; // empty list "[]"
    auto elems = parseTopLevelElements(inner);
    std::sort(elems.begin(), elems.end());
    std::string result = "[";
    for (size_t i = 0; i < elems.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += elems[i];
    }
    result += "]";
    return result;
}

// Get query from doc string (may be null)
std::string getDocString(const std::optional<cucumber_cpp::library::util::DocString>& ds) {
    if (ds.has_value() && !ds->content.empty()) {
        return trim(ds->content);
    }
    return "";
}

} // anonymous namespace

// -----------------------------------------------------------------------
// Hooks
// -----------------------------------------------------------------------

HOOK_BEFORE_ALL() {
    folly::SingletonVault::singleton()->registrationComplete();

    const char* host = std::getenv("EUGRAPH_HOST");
    const char* port = std::getenv("EUGRAPH_PORT");
    if (host)
        TckContext::serverHost = host;
    if (port)
        TckContext::serverPort = std::stoi(port);

    spdlog::info("[TCK] Server config from env: {}:{}", TckContext::serverHost, TckContext::serverPort);
}

HOOK_AFTER_ALL() {
    TckContext::rpc.reset();

    std::cout << "\n[TCK] ==================== SUMMARY ====================\n";
    int total = static_cast<int>(gPassedScenarios.size() + gFailedScenarios.size() + gSkippedScenarios.size());
    std::cout << "[TCK] Total: " << total << "  |  Passed: " << gPassedScenarios.size()
              << "  |  Failed: " << gFailedScenarios.size() << "  |  Skipped: " << gSkippedScenarios.size() << "\n";

    if (!gPassedScenarios.empty()) {
        std::cout << "\n[TCK] === PASSED SCENARIOS ===\n";
        for (const auto& name : gPassedScenarios) {
            std::cout << "[TCK]   PASS: " << name << "\n";
        }
    }
    std::cout << std::endl;

    // Write step-level results to JSON
    const char* stepResultsPath = std::getenv("TCK_STEP_RESULTS_PATH");
    std::string path = stepResultsPath ? stepResultsPath : "tck-step-results.json";
    try {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& r : gStepRecords) {
            nlohmann::json entry;
            entry["scenario"] = r.scenario_name;
            entry["index"] = r.step_index;
            entry["step"] = r.step_text;
            entry["status"] = r.status;
            j.push_back(entry);
        }
        std::ofstream ofs(path);
        ofs << j.dump() << "\n";
        ofs.close();
        std::cout << "[TCK] Step results written to " << path << " (" << gStepRecords.size() << " steps)\n";
    } catch (const std::exception& e) {
        std::cerr << "[TCK] Failed to write step results: " << e.what() << "\n";
    }
}

HOOK_BEFORE_SCENARIO() {
    resetCtx();
    gStepIndex = 0;
    gPendingSteps.clear();
    spdlog::info("[TCK] === Scenario {} (graph: {}) ===", gScenarioNum.load(), gCtx->graphName);
}

HOOK_BEFORE_STEP() {
    gHadFailureBeforeStep = ::testing::Test::HasFailure();
}

HOOK_AFTER_STEP() {
    bool stepFailed = ::testing::Test::HasFailure() && !gHadFailureBeforeStep;
    gPendingSteps.push_back({gCurrentStepText, stepFailed ? "FAILED" : "PASSED"});
    ++gStepIndex;
}

HOOK_AFTER_SCENARIO() {
    // Determine scenario name for reporting
    std::string scenario_name = "#" + std::to_string(gScenarioNum.load());
    if (context.Contains("scenario.name")) {
        scenario_name = context.Get<std::string>("scenario.name");
    }

    if (::testing::Test::IsSkipped()) {
        gSkippedScenarios.push_back(scenario_name);
    } else if (::testing::Test::HasFailure()) {
        gFailedScenarios.push_back(scenario_name);
    } else {
        gPassedScenarios.push_back(scenario_name);
    }

    // Flush pending steps with scenario name
    for (size_t i = 0; i < gPendingSteps.size(); ++i) {
        gStepRecords.push_back({scenario_name, static_cast<int>(i), gPendingSteps[i].first, gPendingSteps[i].second});
    }
    gPendingSteps.clear();

    if (gCtx) {
        gCtx->dropGraph(gCtx->graphName);
    }
}

// -----------------------------------------------------------------------
// Given: initial graph state
// -----------------------------------------------------------------------

GIVEN("^an empty graph$") {
    gCurrentStepText = "an empty graph";
    gCtx->createGraph(gCtx->graphName);
}

GIVEN("^any graph$") {
    gCurrentStepText = "any graph";
    gCtx->createGraph(gCtx->graphName);
}

// -----------------------------------------------------------------------
// Setup query: "And having executed:"
// -----------------------------------------------------------------------

STEP("^having executed:$") {
    gCurrentStepText = "having executed:";
    std::string q = getDocString(docString);
    if (skipIfUnsupported(q)) {
        GTEST_SKIP() << "Unsupported Cypher syntax";
    }
    spdlog::info("[TCK] [{}] setup query: {}", gCtx->graphName, q);
    gCtx->ensureTypesForQuery(q);
    gCtx->executeQuery(q);
}

// -----------------------------------------------------------------------
// Parameters: "And parameters are:"
// -----------------------------------------------------------------------

STEP("^parameters are:$") {
    gCurrentStepText = "parameters are:";
    if (!dataTable || dataTable->rows.empty()) {
        return;
    }
    // TCK parameter table: each row is | param_name | param_value |
    for (const auto& row : dataTable->rows) {
        if (row.cells.size() >= 2) {
            std::string name = trim(row.cells[0].value);
            std::string value = trim(row.cells[1].value);
            gCtx->pendingParams[name] = value;
            spdlog::info("[TCK] [{}] parameter: {} = {}", gCtx->graphName, name, value);
        }
    }
}

// -----------------------------------------------------------------------
// Test query: "When executing query:"
// -----------------------------------------------------------------------

WHEN("^executing query:$") {
    gCurrentStepText = "executing query:";
    std::string q = getDocString(docString);
    if (skipIfUnsupported(q)) {
        GTEST_SKIP() << "Unsupported Cypher syntax";
    }
    spdlog::info("[TCK] [{}] test query: {}", gCtx->graphName, q);
    gCtx->ensureTypesForQuery(q);

    // Snapshot before for side effects
    gCtx->snapshotBefore = gCtx->takeSnapshot();

    gCtx->executeQuery(q);
    gCtx->pendingParams.clear();

    auto after = gCtx->takeSnapshot();
    gCtx->lastSideEffects = gCtx->computeSideEffects(gCtx->snapshotBefore, after);
}

// -----------------------------------------------------------------------
// Control query: "When executing control query:" (verification, no side effects)
// -----------------------------------------------------------------------

WHEN("^executing control query:$") {
    gCurrentStepText = "executing control query:";
    std::string q = getDocString(docString);
    if (skipIfUnsupported(q)) {
        GTEST_SKIP() << "Unsupported Cypher syntax";
    }
    spdlog::info("[TCK] [{}] control query: {}", gCtx->graphName, q);
    gCtx->ensureTypesForQuery(q);
    gCtx->executeQuery(q);
}

// -----------------------------------------------------------------------
// Then: empty result
// -----------------------------------------------------------------------

THEN("^the result should be empty$") {
    gCurrentStepText = "the result should be empty";
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected empty result but got error: " << gCtx->lastErrorType;
    EXPECT_TRUE(gCtx->lastRows.empty()) << "Expected 0 rows, got " << gCtx->lastRows.size();
}

// -----------------------------------------------------------------------
// Then: result in any order (with data table)
// -----------------------------------------------------------------------

THEN("^the result should be, in any order:$") {
    gCurrentStepText = "the result should be, in any order:";
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected result but got error: " << gCtx->lastErrorType;

    if (dataTable->rows.empty()) {
        EXPECT_TRUE(gCtx->lastRows.empty()) << "Expected empty, got " << gCtx->lastRows.size();
        return;
    }

    // First row = column headers, rest = data rows
    std::vector<std::string> expCols;
    for (const auto& c : dataTable->rows[0].cells) {
        expCols.push_back(c.value);
    }
    EXPECT_EQ(gCtx->lastColumns, expCols);

    std::vector<std::vector<std::string>> expRows;
    for (size_t i = 1; i < dataTable->rows.size(); ++i) {
        std::vector<std::string> row;
        for (const auto& c : dataTable->rows[i].cells) {
            row.push_back(c.value);
        }
        if (!row.empty()) {
            expRows.push_back(row);
        }
    }

    ASSERT_EQ(gCtx->lastRows.size(), expRows.size());

    auto sortFunc = [](std::vector<std::vector<std::string>> r) {
        std::sort(r.begin(), r.end());
        return r;
    };

    EXPECT_EQ(sortFunc(gCtx->lastRows), sortFunc(expRows));
}

// -----------------------------------------------------------------------
// Then: result in order (with data table)
// -----------------------------------------------------------------------

THEN("^the result should be, in order:$") {
    gCurrentStepText = "the result should be, in order:";
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected result but got error: " << gCtx->lastErrorType;

    if (dataTable->rows.empty()) {
        EXPECT_TRUE(gCtx->lastRows.empty());
        return;
    }

    std::vector<std::string> expCols;
    for (const auto& c : dataTable->rows[0].cells) {
        expCols.push_back(c.value);
    }
    EXPECT_EQ(gCtx->lastColumns, expCols);

    std::vector<std::vector<std::string>> expRows;
    for (size_t i = 1; i < dataTable->rows.size(); ++i) {
        std::vector<std::string> row;
        for (const auto& c : dataTable->rows[i].cells) {
            row.push_back(c.value);
        }
        if (!row.empty()) {
            expRows.push_back(row);
        }
    }

    ASSERT_EQ(gCtx->lastRows.size(), expRows.size());
    EXPECT_EQ(gCtx->lastRows, expRows);
}

// -----------------------------------------------------------------------
// Then: error assertions  (TYPE at PHASE: DETAIL)
// -----------------------------------------------------------------------

// Pattern: "a SyntaxError should be raised at compile time: UndefinedVariable"
THEN(
    R"(^a (SyntaxError|SemanticError|TypeError|ArgumentError|ArithmeticError|EntityNotFound|PropertyNotFound|LabelNotFound|ConstraintVerificationFailed|ConstraintValidationFailed|ParameterMissing) should be raised at (compile time|runtime|any time): (.+)$)",
    (const std::string& expType, const std::string& expPhase, const std::string& expDetail)) {
    gCurrentStepText = "an error should be raised at ...:";
    ASSERT_TRUE(gCtx->lastQueryHadError) << "Expected error " << expType << " but query succeeded";
    EXPECT_EQ(gCtx->lastErrorType, expType);
    if (expPhase != "any time") {
        EXPECT_EQ(gCtx->lastErrorPhase, expPhase);
    }
    if (expDetail != "*") {
        EXPECT_EQ(gCtx->lastErrorDetail, expDetail);
    }
}

// -----------------------------------------------------------------------
// And: side effects (with data table)
// -----------------------------------------------------------------------

STEP("^the side effects should be:$") {
    gCurrentStepText = "the side effects should be:";
    // Each row is | metric | value | (including first row)
    std::map<std::string, int64_t> expected;
    for (const auto& row : dataTable->rows) {
        if (row.cells.size() >= 2) {
            expected[row.cells[0].value] = std::stoll(row.cells[1].value);
        }
    }

    SideEffects& se = gCtx->lastSideEffects;

    EXPECT_EQ(se.nodes, expected.count("+nodes") ? expected["+nodes"] : 0);
    EXPECT_EQ(se.relationships, expected.count("+relationships") ? expected["+relationships"] : 0);
    EXPECT_EQ(se.labels, expected.count("+labels") ? expected["+labels"] : 0);
    EXPECT_EQ(se.properties, expected.count("+properties") ? expected["+properties"] : 0);
}

// -----------------------------------------------------------------------
// And: no side effects
// -----------------------------------------------------------------------

STEP("^no side effects$") {
    gCurrentStepText = "no side effects";
    SideEffects& se = gCtx->lastSideEffects;
    EXPECT_TRUE(se.isZero()) << "Expected no side effects but got: "
                             << "nodes=" << se.nodes << " rels=" << se.relationships << " labels=" << se.labels
                             << " props=" << se.properties;
}

// -----------------------------------------------------------------------
// Then: result ignoring element order within lists (rows in any order)
// -----------------------------------------------------------------------

THEN(R"(^the result should be \(ignoring element order for lists\):$)") {
    gCurrentStepText = "the result should be (ignoring element order for lists):";
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected result but got error: " << gCtx->lastErrorType;

    if (dataTable->rows.empty()) {
        EXPECT_TRUE(gCtx->lastRows.empty()) << "Expected empty, got " << gCtx->lastRows.size();
        return;
    }

    std::vector<std::string> expCols;
    for (const auto& c : dataTable->rows[0].cells) {
        expCols.push_back(c.value);
    }
    EXPECT_EQ(gCtx->lastColumns, expCols);

    std::vector<std::vector<std::string>> expRows;
    for (size_t i = 1; i < dataTable->rows.size(); ++i) {
        std::vector<std::string> row;
        for (const auto& c : dataTable->rows[i].cells) {
            row.push_back(c.value);
        }
        if (!row.empty()) {
            expRows.push_back(row);
        }
    }

    ASSERT_EQ(gCtx->lastRows.size(), expRows.size());

    // Normalize list order within cells, then sort rows for order-independent comparison
    auto normalizeRow = [](std::vector<std::vector<std::string>> rows) {
        for (auto& row : rows) {
            for (auto& cell : row) {
                cell = normalizeListOrder(cell);
            }
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    };

    EXPECT_EQ(normalizeRow(gCtx->lastRows), normalizeRow(expRows));
}

// -----------------------------------------------------------------------
// Then: result in order, ignoring element order within lists
// -----------------------------------------------------------------------

THEN(R"(^the result should be, in order \(ignoring element order for lists\):$)") {
    gCurrentStepText = "the result should be, in order (ignoring element order for lists):";
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected result but got error: " << gCtx->lastErrorType;

    if (dataTable->rows.empty()) {
        EXPECT_TRUE(gCtx->lastRows.empty());
        return;
    }

    std::vector<std::string> expCols;
    for (const auto& c : dataTable->rows[0].cells) {
        expCols.push_back(c.value);
    }
    EXPECT_EQ(gCtx->lastColumns, expCols);

    std::vector<std::vector<std::string>> expRows;
    for (size_t i = 1; i < dataTable->rows.size(); ++i) {
        std::vector<std::string> row;
        for (const auto& c : dataTable->rows[i].cells) {
            row.push_back(c.value);
        }
        if (!row.empty()) {
            expRows.push_back(row);
        }
    }

    ASSERT_EQ(gCtx->lastRows.size(), expRows.size());

    // Normalize list order within cells, keep row order
    auto normalizeCells = [](std::vector<std::vector<std::string>> rows) {
        for (auto& row : rows) {
            for (auto& cell : row) {
                cell = normalizeListOrder(cell);
            }
        }
        return rows;
    };

    EXPECT_EQ(normalizeCells(gCtx->lastRows), normalizeCells(expRows));
}
