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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <thread>

// POSIX for server management
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

using namespace eugraph::tck;

// -----------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------

namespace {

std::unique_ptr<TckContext> gCtx;
std::atomic<int> gScenarioNum{0};
pid_t gServerPid = 0;

void startServer() {
    const char* path = EUGRAPH_SERVER_PATH;
    int port = 9999;

    gServerPid = fork();
    if (gServerPid < 0) {
        spdlog::error("[TCK] fork failed: {}", strerror(errno));
        return;
    }

    if (gServerPid == 0) {
        // Redirect stdin/stdout/stderr to server log file
        int logfd = open("/tmp/eugraph_tck_server.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            if (logfd > 2)
                close(logfd);
        }
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > 2)
                close(devnull);
        }

        // Close inherited fds so pipe/socket writers in parent see EOF
        DIR* d = opendir("/proc/self/fd");
        if (d) {
            int dfd = ::dirfd(d);
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (ent->d_name[0] == '.')
                    continue;
                int fd = atoi(ent->d_name);
                if (fd > 2 && fd != dfd)
                    close(fd);
            }
            closedir(d);
        }

        execl(path, path, "--port", std::to_string(port).c_str(), "--data-dir", "/tmp/eugraph_tck_data", nullptr);
        // execl failed
        std::cerr << "exec failed: " << strerror(errno) << std::endl;
        _exit(1);
    }

    spdlog::info("[TCK] Server starting pid={} port={}", gServerPid, port);

    TckContext::serverHost = "127.0.0.1";
    TckContext::serverPort = port;

    // Poll until server is ready or dies (raw TCP probe, no Folly/Thrift)
    static constexpr auto kTimeout = std::chrono::seconds(15);
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        int status;
        pid_t result = waitpid(gServerPid, &status, WNOHANG);
        if (result == gServerPid) {
            spdlog::error("[TCK] Server died before accepting connections "
                          "(exit={}, signal={})",
                          WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            gServerPid = 0;
            return;
        }

        // Raw TCP connect to check if server is accepting
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(TckContext::serverPort));
            inet_pton(AF_INET, TckContext::serverHost.c_str(), &addr.sin_addr);

            struct timeval tv {
                1, 0
            }; // 1s timeout
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
                close(sock);
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                        .count();
                spdlog::info("[TCK] Server ready ({}ms)", elapsed);
                break;
            }
            close(sock);
        }

        if (std::chrono::steady_clock::now() - start > kTimeout) {
            spdlog::error("[TCK] Server startup timed out");
            kill(gServerPid, SIGKILL);
            waitpid(gServerPid, nullptr, 0);
            gServerPid = 0;
            return;
        }
    }
}

void stopServer() {
    if (gServerPid > 0) {
        kill(gServerPid, SIGTERM);
        int status;
        waitpid(gServerPid, &status, 0);
        spdlog::info("[TCK] Server stopped pid={} exit={}", gServerPid, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        gServerPid = 0;
    }
}

// Check if server process is still alive
static bool isServerProcessAlive() {
    if (gServerPid == 0)
        return false;
    int status;
    pid_t result = waitpid(gServerPid, &status, WNOHANG);
    if (result == gServerPid) {
        spdlog::info("[TCK] Server pid={} has exited (exit={}, signal={})", gServerPid,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        gServerPid = 0;
        return false;
    }
    return true;
}

// Check if server TCP port is accepting connections
static bool isServerPortOpen() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(TckContext::serverPort));
    inet_pton(AF_INET, TckContext::serverHost.c_str(), &addr.sin_addr);
    struct timeval tv {
        0, 500000
    }; // 500ms timeout
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int ret = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    close(sock);
    return ret == 0;
}

// Ensure the server is alive, restart if needed
static void ensureServerAlive() {
    if (isServerProcessAlive() && isServerPortOpen())
        return;

    spdlog::warn("[TCK] Server is dead, restarting...");
    TckContext::rpc.reset();

    // Kill existing zombie if any
    if (gServerPid > 0) {
        kill(gServerPid, SIGKILL);
        waitpid(gServerPid, nullptr, 0);
        gServerPid = 0;
    }

    startServer();
}

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

void skipIfUnsupported(const std::string& query) {
    if (!TckContext::isQuerySupported(query)) {
        GTEST_SKIP() << "Unsupported Cypher syntax";
    }
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
    // Ensure Folly singletons are initialized before any RPC calls
    folly::SingletonVault::singleton()->registrationComplete();
    startServer();
}

HOOK_AFTER_ALL() {
    TckContext::rpc.reset();
    stopServer();
}

HOOK_BEFORE_SCENARIO() {
    ensureServerAlive();
    resetCtx();
}

HOOK_AFTER_SCENARIO() {
    if (gCtx) {
        gCtx->dropGraph(gCtx->graphName);
    }
}

// -----------------------------------------------------------------------
// Given: initial graph state
// -----------------------------------------------------------------------

GIVEN("^an empty graph$") {
    gCtx->createGraph(gCtx->graphName);
}

GIVEN("^any graph$") {
    gCtx->createGraph(gCtx->graphName);
}

// -----------------------------------------------------------------------
// Setup query: "And having executed:"
// -----------------------------------------------------------------------

STEP("^having executed:$") {
    std::string q = getDocString(docString);
    skipIfUnsupported(q);
    spdlog::info("[TCK] setup query: {}", q);
    gCtx->ensureTypesForQuery(q);
    gCtx->executeQuery(q);
}

// -----------------------------------------------------------------------
// Test query: "When executing query:"
// -----------------------------------------------------------------------

WHEN("^executing query:$") {
    std::string q = getDocString(docString);
    skipIfUnsupported(q);
    spdlog::info("[TCK] test query: {}", q);
    gCtx->ensureTypesForQuery(q);

    // Snapshot before for side effects
    gCtx->snapshotBefore = gCtx->takeSnapshot();

    gCtx->executeQuery(q);

    auto after = gCtx->takeSnapshot();
    gCtx->lastSideEffects = gCtx->computeSideEffects(gCtx->snapshotBefore, after);
}

// -----------------------------------------------------------------------
// Then: empty result
// -----------------------------------------------------------------------

THEN("^the result should be empty$") {
    ASSERT_FALSE(gCtx->lastQueryHadError) << "Expected empty result but got error: " << gCtx->lastErrorType;
    EXPECT_TRUE(gCtx->lastRows.empty()) << "Expected 0 rows, got " << gCtx->lastRows.size();
}

// -----------------------------------------------------------------------
// Then: result in any order (with data table)
// -----------------------------------------------------------------------

THEN("^the result should be, in any order:$") {
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
    R"(^a (SyntaxError|SemanticError|TypeError|ArgumentError|ArithmeticError|EntityNotFound|PropertyNotFound|LabelNotFound|ConstraintVerificationFailed|ConstraintValidationFailed|ParameterMissing) should be raised at (compile time|runtime): (.+)$)",
    (const std::string& expType, const std::string& expPhase, const std::string& expDetail)) {
    ASSERT_TRUE(gCtx->lastQueryHadError) << "Expected error " << expType << " but query succeeded";
    EXPECT_EQ(gCtx->lastErrorType, expType);
    EXPECT_EQ(gCtx->lastErrorPhase, expPhase);
    EXPECT_EQ(gCtx->lastErrorDetail, expDetail);
}

// -----------------------------------------------------------------------
// And: side effects (with data table)
// -----------------------------------------------------------------------

STEP("^the side effects should be:$") {
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
    SideEffects& se = gCtx->lastSideEffects;
    EXPECT_TRUE(se.isZero()) << "Expected no side effects but got: "
                             << "nodes=" << se.nodes << " rels=" << se.relationships << " labels=" << se.labels
                             << " props=" << se.properties;
}
