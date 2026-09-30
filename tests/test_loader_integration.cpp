// Loader 端到端集成测试：schema JSON → 点 → 索引 → 边（主键解析）
//
// 这些用例的判据都必须在缺陷存在时失败：
//   - 属性落在 schema 键（主标签）下，行级标签只是额外标签；
//   - 主键唯一索引在装载期就可用（边端点靠它解析）；
//   - 复合主键按元组解析；
//   - 端点标签缺失/未声明主键 → 启动即报错，而不是运行期静默跳过边。
#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "program/loader/csv_loader.hpp"
#include "program/shell/rpc_client.hpp"
#include "query/executor/query_executor.hpp"
#include "service/graph_service.hpp"
#include "service/thrift/eugraph_handler.hpp"
#include "service/thrift/gen-cpp2/eugraph_types.h"
#include "service/thrift/result_format.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/graph_manager.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <folly/SocketAddress.h>
#include <folly/coro/BlockingWait.h>
#include <fstream>

#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

using namespace eugraph;
using namespace eugraph::thrift_service;
using namespace folly::coro;

namespace {

class LoaderE2ETest : public ::testing::Test {
protected:
    std::string work_dir_;
    std::shared_ptr<GraphManager> graph_manager_;
    std::shared_ptr<service::GraphService> graph_service_;
    std::shared_ptr<service::thrift::EuGraphHandler> handler_;
    std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
    std::unique_ptr<shell::EuGraphRpcClient> client_;

    void SetUp() override {
        // 唯一临时目录：不写死 /tmp（TMPDIR 可能不同），也不只用 getpid
        // （并行 ctest 下同进程多用例会共用同一路径）。
        auto tmpl = (std::filesystem::temp_directory_path() / "eugraph_loader_e2e_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        ASSERT_NE(::mkdtemp(buf.data()), nullptr) << "mkdtemp failed: " << tmpl;
        work_dir_ = buf.data();
        std::filesystem::create_directories(work_dir_ + "/data");

        graph_manager_ = std::make_shared<GraphManager>();
        ASSERT_TRUE(graph_manager_->init(work_dir_ + "/db", 2, 2));

        graph_service_ = std::make_shared<service::GraphService>(*graph_manager_);
        handler_ = std::make_shared<service::thrift::EuGraphHandler>(*graph_service_);

        auto ts = std::make_shared<apache::thrift::ThriftServer>();
        // 显式 IPv4 回环：CI 容器里 IPv6 回环不一定可用，且失败信息会很含糊。
        ts->setAddress(folly::SocketAddress("127.0.0.1", 0));
        ts->setInterface(handler_);
        ts->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
        ts->setMaxFinishedDebugPayloadsPerWorker(0);
        ts->setThreadManagerFromExecutor(ts->getIOThreadPool().get());
        server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(ts);

        client_ = std::make_unique<shell::EuGraphRpcClient>("127.0.0.1", server_->getPort());
        ASSERT_TRUE(client_->connect()) << "connect to 127.0.0.1:" << server_->getPort() << " failed";
    }

    void TearDown() override {
        client_.reset();
        // 这里刻意用 release() 而非 reset()：本夹具把 ThriftServer 的 thread manager
        // 换成了它自己的 IO 线程池，「析构」与「先 stop() 再析构」两种写法实测都会卡在
        // join 上。代价是每个用例泄漏一个事件循环线程（进程结束即回收，用例数封顶）。
        // 要根治得单独排查该 thread manager 的关闭顺序，不要在别处顺手改。
        (void)server_.release();
        handler_.reset();
        graph_service_.reset();
        graph_manager_->shutdown();
        graph_manager_.reset();
        std::filesystem::remove_all(work_dir_);
    }

    void writeFile(const std::string& rel, const std::string& content) {
        auto path = std::filesystem::path(work_dir_) / "data" / rel;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        ofs << content;
    }

    void writeSchema(const std::string& content) {
        writeFile("../../schema.json", "placeholder"); // 占位，避免被当成数据文件
        std::filesystem::remove(std::filesystem::path(work_dir_) / "schema.json");
        std::ofstream ofs(std::filesystem::path(work_dir_) / "schema.json", std::ios::trunc);
        ofs << content;
    }

    loader::SchemaConfig loadConfig(bool strict_types = true) {
        return loader::loadSchemaConfig(std::filesystem::path(work_dir_) / "schema.json", work_dir_ + "/data",
                                        strict_types);
    }

    /// 执行 Cypher 并返回「第一行第一列」的格式化文本；查询报错或空结果返回 nullopt。
    std::optional<std::string> querySingle(const std::string& cypher) {
        try {
            auto [meta, stream] = client_->executeCypher(cypher, "default");
            auto gen = std::move(stream).toAsyncGenerator();
            std::optional<std::string> out;
            folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
                while (auto batch = co_await gen.next()) {
                    for (const auto& row : *batch->rows()) {
                        const auto& vals = *row.values();
                        if (!out.has_value() && !vals.empty())
                            out = eugraph::service::thrift::formatResultValue(vals[0]);
                    }
                }
            }());
            return out;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::string runLoad() {
        auto config = loadConfig();
        loader::createLabels(*client_, config);
        loader::createEdgeLabels(*client_, config);
        loader::createPrimaryKeyIndexes(*client_, config);
        std::vector<shell::EuGraphRpcClient*> clients{client_.get()};
        auto [vw, vd] = loader::loadVertices(clients, config, 100, 1);
        auto [ew, es] = loader::loadEdges(clients, config, 100, 1);
        return "vertices=" + std::to_string(vw) + " dup=" + std::to_string(vd) + " edges=" + std::to_string(ew) +
               " skipped=" + std::to_string(es);
    }
};

// ==================== 基础装载 + 主键解析 ====================

TEST_F(LoaderE2ETest, LoadsVerticesAndResolvesEdgesByPrimaryKey) {
    writeFile("person.csv", "id|name\n1|alice\n2|bob\n");
    writeFile("knows.csv", "Person.id|Person.id|since\n1|2|2020\n2|1|2021\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "person.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": { "knows": [ { "file": "knows.csv", "src": "Person.id", "dst": "Person.id",
                                      "src_label": "person", "dst_label": "person",
                                      "columns": { "since": "INT64" } } ] }
    })");

    auto summary = runLoad();
    // 缺陷（端点解析不到）时 edges 会是 0、skipped 会是 2
    EXPECT_NE(summary.find("edges=2"), std::string::npos) << summary;
    EXPECT_NE(summary.find("skipped=0"), std::string::npos) << summary;

    auto count = querySingle("MATCH (a:person)-[:knows]->(b:person) RETURN count(*)");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, "2");

    // 主键唯一索引让点查可走索引（结果正确即可验证索引里确实有条目）
    auto by_id = querySingle("MATCH (p:person {id: 2}) RETURN p.name");
    ASSERT_TRUE(by_id.has_value());
    EXPECT_NE(by_id->find("bob"), std::string::npos) << *by_id;
}

// ==================== 属性重命名 + 行级标签不抢主标签 ====================

TEST_F(LoaderE2ETest, RenamesPropertiesAndKeepsRowLabelAsExtra) {
    writeFile("place.csv", "id|name|url|type\n1|India|http://x|country\n2|Delhi|http://y|city\n");
    writeFile("person.csv", "id\n1\n");
    writeFile("person_located.csv", "Person.id|Place.id\n1|1\n1|2\n");
    writeSchema(R"({
      "labels": { "place": [ { "file": "place.csv", "pk": "id",
                               "columns": { "id": "INT64", "name": "STRING", "url": "STRING" },
                               "label": [{ "header": "type" }] } ],
                  "person": [ { "file": "person.csv", "pk": "id",
                                "columns": { "id": "INT64" } } ] },
      "relationships": { "isLocatedIn": [ { "file": "person_located.csv", "src": "Person.id", "dst": "Place.id",
                                            "src_label": "person", "dst_label": "place" } ] }
    })");

    runLoad();

    // type 列只作行级标签 → 不产生属性；顶点带 [place, country/city] 两个标签
    auto country = querySingle("MATCH (n:country) RETURN count(*)");
    ASSERT_TRUE(country.has_value());
    EXPECT_EQ(*country, "1");

    // 属性在 schema 键（place）下，因此按 place 查得到
    auto by_place = querySingle("MATCH (n:place {name: 'India'}) RETURN count(*)");
    ASSERT_TRUE(by_place.has_value());
    EXPECT_EQ(*by_place, "1");

    // 行级标签同样能查到同一顶点
    auto by_country = querySingle("MATCH (n:country {name: 'India'}) RETURN count(*)");
    ASSERT_TRUE(by_country.has_value());
    EXPECT_EQ(*by_country, "1");

    // 边靠 place 的复合/单列唯一索引解析成功
    auto edges = querySingle("MATCH (:person)-[:isLocatedIn]->(:place) RETURN count(*)");
    ASSERT_TRUE(edges.has_value());
    EXPECT_EQ(*edges, "2");
}

TEST_F(LoaderE2ETest, RenamedPropertyKeepsSourceColumn) {
    writeFile("org.csv", "id|type|name\n7|company|Acme\n");
    writeSchema(R"({
      "labels": { "organisation": [ { "file": "org.csv", "pk": "id",
                                      "columns": { "id": "INT64",
                                                   "kind": { "header": "type", "type": "STRING" },
                                                   "name": "STRING" },
                                      "label": [{ "header": "type" }] } ] },
      "relationships": {}
    })");
    runLoad();

    auto kind = querySingle("MATCH (o:organisation {kind: 'company'}) RETURN o.name");
    ASSERT_TRUE(kind.has_value());
    EXPECT_NE(kind->find("Acme"), std::string::npos) << *kind;
}

// ==================== 数据类型声明（不再靠推断） ====================

TEST_F(LoaderE2ETest, DeclaredTypesAreRespected) {
    writeFile("t.csv", "id|tags|score|flag\n1|a;b|1.5|true\n");
    writeSchema(R"({
      "labels": { "thing": [ { "file": "t.csv", "pk": "id",
                               "columns": { "id": "INT64", "tags": "STRING[]",
                                            "score": "DOUBLE", "flag": "BOOL" } } ] },
      "relationships": {}
    })");
    runLoad();

    auto tags = querySingle("MATCH (t:thing) RETURN size(t.tags)");
    ASSERT_TRUE(tags.has_value());
    EXPECT_EQ(*tags, "2");
    auto score = querySingle("MATCH (t:thing) RETURN t.score");
    ASSERT_TRUE(score.has_value());
    EXPECT_NE(score->find("1.5"), std::string::npos) << *score;
}

// ==================== 复合主键 ====================

TEST_F(LoaderE2ETest, CompositePrimaryKeyResolvesEndpoints) {
    // membership 由 (tenantId, userId) 唯一确定；同一 userId 不同 tenant 是两个顶点
    writeFile("membership.csv", "tenant|id\n1|5\n2|5\n");
    writeFile("user.csv", "id|name\n5|alice\n");
    writeFile("belongs.csv", "tenant|id|uid\n1|5|5\n2|5|5\n");
    writeSchema(R"({
      "labels": { "membership": [ { "file": "membership.csv", "pk": ["tenantId", "userId"],
                                    "columns": { "tenantId": { "header": "tenant", "type": "STRING" },
                                                 "userId": { "header": "id", "type": "INT64" } } } ],
                  "user": [ { "file": "user.csv", "pk": "id",
                              "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": { "belongsTo": [ { "file": "belongs.csv",
                                          "src": [ "tenant", "id" ],
                                          "dst": "uid",
                                          "src_label": "membership", "dst_label": "user" } ] }
    })");

    auto summary = runLoad();
    // 复合键元组顺序错位时端点会全部解析不到
    EXPECT_NE(summary.find("edges=2"), std::string::npos) << summary;
    EXPECT_NE(summary.find("skipped=0"), std::string::npos) << summary;

    auto memberships = querySingle("MATCH (m:membership) RETURN count(*)");
    ASSERT_TRUE(memberships.has_value());
    EXPECT_EQ(*memberships, "2"); // 同一 userId 不同 tenant 都保留

    auto edges = querySingle("MATCH (:membership)-[:belongsTo]->(:user) RETURN count(*)");
    ASSERT_TRUE(edges.has_value());
    EXPECT_EQ(*edges, "2");
}

// ==================== first-wins：重复主键不阻断装载 ====================

TEST_F(LoaderE2ETest, DuplicatePrimaryKeyIsSkippedAndIndexStaysClean) {
    writeFile("p.csv", "id|name\n1|first\n1|second\n2|third\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": {}
    })");
    auto summary = runLoad();
    EXPECT_NE(summary.find("vertices=2"), std::string::npos) << summary;
    EXPECT_NE(summary.find("dup=1"), std::string::npos) << summary;

    // 唯一索引仍可用（预检生效：冲突的顶点根本没写进去）
    auto by_id = querySingle("MATCH (p:person {id: 1}) RETURN p.name");
    ASSERT_TRUE(by_id.has_value());
    EXPECT_NE(by_id->find("first"), std::string::npos) << *by_id;
}

// ==================== 配置校验：启动即报错 ====================

TEST_F(LoaderE2ETest, RejectsEndpointLabelWithoutPrimaryKey) {
    writeFile("a.csv", "id|name\n1|x\n");
    writeFile("b.csv", "id|a_id\n1|1\n");
    // thing 未声明主键 → 被边引用时必须报错（缺陷存在时会静默按 0 端点写入或跳过整份边文件）
    writeSchema(R"({
      "labels": { "thing": [ { "file": "a.csv", "columns": { "id": "INT64", "name": "STRING" } } ],
                  "edge":  [ { "file": "b.csv", "pk": "id",
                               "columns": { "id": "INT64", "a_id": "INT64" } } ] },
      "relationships": { "rel": [ { "file": "b.csv", "src": "id", "dst": "a_id",
                                    "src_label": "edge", "dst_label": "thing" } ] }
    })");
    try {
        loadConfig();
        FAIL() << "expected missing primary key on referenced label to be rejected";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("primary key"), std::string::npos) << e.what();
    }
}

TEST_F(LoaderE2ETest, RejectsUndeclaredEndpointLabel) {
    writeFile("a.csv", "id\n1\n");
    writeSchema(R"({
      "labels": { "thing": [ { "file": "a.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": { "rel": [ { "file": "a.csv", "src": "id", "dst": "id",
                                    "src_label": "thing", "dst_label": "nonexistent" } ] }
    })");
    try {
        loadConfig();
        FAIL() << "expected undeclared endpoint label to be rejected";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("not declared"), std::string::npos) << e.what();
    }
}

TEST_F(LoaderE2ETest, RejectsPrimaryKeyNotDeclaredInColumns) {
    writeFile("a.csv", "id\n1\n");
    writeSchema(R"({
      "labels": { "thing": [ { "file": "a.csv", "pk": "nope", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected pk referencing an undeclared property to be rejected";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("not declared"), std::string::npos) << e.what();
    }
}

TEST_F(LoaderE2ETest, RejectsMissingDataFile) {
    writeSchema(R"({
      "labels": { "thing": [ { "file": "missing.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected missing data file to be rejected";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("not found"), std::string::npos) << e.what();
    }
}

// ==================== strict / 非 strict（--no-schema-strict） ====================

TEST_F(LoaderE2ETest, StrictRejectsUndeclaredColumns) {
    writeFile("p.csv", "id|name|score\n1|alice|7\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected undeclared columns to be rejected in strict mode";
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("not declared"), std::string::npos) << msg;
        EXPECT_NE(msg.find("name"), std::string::npos) << msg; // 必须指出具体列名
        EXPECT_NE(msg.find("score"), std::string::npos) << msg;
    }
}

TEST_F(LoaderE2ETest, NonStrictInfersUndeclaredColumns) {
    writeFile("p.csv", "id|name|score\n1|alice|7\n2|bob|9\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    auto config = loadConfig(/*strict_types=*/false);
    // 未声明的列被补上：name=STRING（非全整数），score=INT64（全整数）
    const auto& merged = config.merged_properties.at("person");
    auto type_of = [&](const std::string& prop) -> std::optional<loader::CsvColumnType> {
        for (const auto& c : merged)
            if (c.name == prop)
                return c.type;
        return std::nullopt;
    };
    ASSERT_TRUE(type_of("name").has_value());
    EXPECT_EQ(*type_of("name"), loader::CsvColumnType::STRING);
    ASSERT_TRUE(type_of("score").has_value());
    EXPECT_EQ(*type_of("score"), loader::CsvColumnType::INT64);
}

// ==================== 文件级 delimiter 覆盖 ====================

// 判据：同一批文件用**不同分隔符**，全局默认只对其中一个成立。
// 若文件级覆盖没生效，另一个文件会整体被当成单列 → 列数校验失败或字段对不上。
TEST_F(LoaderE2ETest, PerFileDelimiterOverridesGlobal) {
    writeFile("a.csv", "id|name\n1|alice\n"); // 用全局默认 '|'
    writeFile("b.csv", "id::score\n2::99\n"); // 用文件级 '::'（多字符）
    writeSchema(R"({
      "delimiter": "|",
      "labels": {
        "person": [ { "file": "a.csv", "pk": "id",
                      "columns": { "id": "INT64", "name": "STRING" } } ],
        "thing":  [ { "file": "b.csv", "pk": "id", "delimiter": "::",
                      "columns": { "id": "INT64", "score": "INT64" } } ]
      },
      "relationships": {}
    })");
    auto summary = runLoad();
    EXPECT_NE(summary.find("vertices=2"), std::string::npos) << summary;

    auto name = querySingle("MATCH (p:person) RETURN p.name");
    ASSERT_TRUE(name.has_value());
    EXPECT_NE(name->find("alice"), std::string::npos) << *name;

    // '::' 被整体当作分隔符：score 必须是 99（若被逐字符切分则解析不出这个值）
    auto score = querySingle("MATCH (t:thing) RETURN t.score");
    ASSERT_TRUE(score.has_value());
    EXPECT_NE(score->find("99"), std::string::npos) << *score;
}

// ==================== 文件级 date_format 覆盖 ====================

// 判据：两个文件的时间列单位不同（ms / s），各自按自己的 date_format 解析。
// 期望值来自外部换算（1700000000s == 1700000000000ms），不是自己推断。
TEST_F(LoaderE2ETest, PerFileDateFormatOverridesGlobal) {
    writeFile("ms.csv", "id|creationDate\n1|1700000000000\n");
    writeFile("sec.csv", "id|creationDate\n2|1700000000\n");
    writeSchema(R"({
      "date_format": "epoch_ms",
      "labels": {
        "msEvent":  [ { "file": "ms.csv", "pk": "id",
                        "columns": { "id": "INT64", "creationDate": "DATETIME" } } ],
        "secEvent": [ { "file": "sec.csv", "pk": "id", "date_format": "epoch_s",
                        "columns": { "id": "INT64", "creationDate": "DATETIME" } } ]
      },
      "relationships": {}
    })");
    auto summary = runLoad();
    EXPECT_NE(summary.find("vertices=2"), std::string::npos) << summary;

    // 两个文件写的是**同一时刻**（1700000000000ms == 1700000000s）。
    // 判据用外部换算：正确解析时二者相差 0 毫秒；若文件级 date_format 未生效
    // （sec.csv 被按 epoch_ms 解释），差值会是 1699999998300 秒级的巨大偏移。
    auto diff = querySingle("MATCH (a:msEvent), (b:secEvent) "
                            "RETURN duration.inSeconds(b.creationDate, a.creationDate).seconds");
    ASSERT_TRUE(diff.has_value()) << "duration.inSeconds 查询失败（时间列可能未解析）";
    ASSERT_EQ(diff->find("null"), std::string::npos) << "时间列为 null: " << *diff;
    // 必须解析成**数值 0**：不能用 find('0') 之类的子串判断
    // （错误解析得到的 1698300000 也含 '0'，会被误判为通过）。
    int64_t seconds = -1;
    try {
        seconds = std::stoll(*diff);
    } catch (const std::exception&) {
        FAIL() << "差值的返回值无法解析为整数: " << *diff;
    }
    EXPECT_EQ(seconds, 0) << "两个文件的时间未解析为同一时刻（文件级 date_format 未生效）: " << *diff;
}

// ==================== undeclared_files ====================

TEST_F(LoaderE2ETest, UndeclaredFilesIgnoreSuppressesFailure) {
    writeFile("a.csv", "id\n1\n");
    writeFile("extra.csv", "id\n1\n");
    writeSchema(R"({
      "undeclared_files": "ignore",
      "labels": { "thing": [ { "file": "a.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    auto config = loadConfig();
    EXPECT_EQ(config.undeclared_files, "ignore");
    // 未声明文件仍能被列出（只是调用方不再据此失败）
    EXPECT_EQ(loader::findUndeclaredCsvFiles(config).size(), 1u);
}

TEST_F(LoaderE2ETest, UndeclaredFilesDefaultsToError) {
    writeFile("a.csv", "id\n1\n");
    writeSchema(R"({
      "labels": { "thing": [ { "file": "a.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    EXPECT_EQ(loadConfig().undeclared_files, "error");
}

TEST_F(LoaderE2ETest, RejectsBadDelimiterInSchema) {
    writeFile("a.csv", "id\n1\n");
    writeSchema(R"({
      "delimiter": "   ",
      "labels": { "thing": [ { "file": "a.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    EXPECT_THROW(loadConfig(), std::runtime_error);
}

// ==================== 多来源行级标签（label 数组） ====================

// 判据：一个文件两个列各自贡献标签。只支持单列时 tier 那个标签根本不存在。
TEST_F(LoaderE2ETest, MultipleLabelColumnsEachContribute) {
    writeFile("p.csv", "id|name|type|tier\n1|Delhi|city|gold\n2|India|country|silver\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "name": "STRING", "type": "STRING", "tier": "STRING" },
                               "label": [ { "header": "type", "case": "capitalize" },
                                          { "header": "tier", "case": "capitalize" } ] } ] },
      "relationships": {}
    })");
    runLoad();

    // 两个来源都生效
    auto both = querySingle("MATCH (n:City:Gold) RETURN count(n)");
    ASSERT_TRUE(both.has_value()) << "City/Tier 标签未同时生效";
    EXPECT_EQ(*both, "1");

    // 第二个来源（tier）单独也能查到 —— 单列实现下这里必然是 0
    auto tier = querySingle("MATCH (n:Silver) RETURN count(n)");
    ASSERT_TRUE(tier.has_value());
    EXPECT_EQ(*tier, "1");

    auto country = querySingle("MATCH (n:Country) RETURN count(n)");
    ASSERT_TRUE(country.has_value());
    EXPECT_EQ(*country, "1");
}

// 判据：静态标签与列来源混合（你选定的写法）。
TEST_F(LoaderE2ETest, StaticAndColumnSourcesMix) {
    writeFile("p.csv", "id|type\n1|city\n2|country\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "type": "STRING" },
                               "label": [ { "derived": ["Entity"] },
                                          { "header": "type", "case": "capitalize" } ] } ] },
      "relationships": {}
    })");
    runLoad();

    // 每个顶点都带静态标签 Entity，同时各自带列来源的标签
    auto entity = querySingle("MATCH (n:Entity) RETURN count(n)");
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(*entity, "2");
    auto combo = querySingle("MATCH (n:Entity:City) RETURN count(n)");
    ASSERT_TRUE(combo.has_value());
    EXPECT_EQ(*combo, "1");
}

// 判据：多个来源产出同名标签时**去重**（不去重会变成 2 个标签，但 Cypher 的
// :City 查询仍返回 1 个顶点，所以这里用 labels() 的基数来判断）。
TEST_F(LoaderE2ETest, DuplicateRowLabelsAreDeduplicated) {
    writeFile("p.csv", "id|type\n1|city\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "type": "STRING" },
                               "label": [ { "derived": ["City"] },
                                          { "header": "type", "case": "capitalize" } ] } ] },
      "relationships": {}
    })");
    runLoad();

    auto n = querySingle("MATCH (n:Place) RETURN size(labels(n))");
    ASSERT_TRUE(n.has_value()) << "labels() 查询失败";
    EXPECT_EQ(*n, "2") << "应为 [Place, City]（City 只出现一次）: " << *n;
}

// 判据：列值等于主标签名时丢弃，不重复贴主标签。
TEST_F(LoaderE2ETest, RowLabelEqualToPrimaryIsDropped) {
    writeFile("p.csv", "id|type\n1|place\n2|city\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "type": "STRING" },
                               "label": [ { "header": "type", "case": "capitalize" } ] } ] },
      "relationships": {}
    })");
    runLoad();

    auto rows = querySingle("MATCH (n:Place {id: 1}) RETURN size(labels(n))");
    ASSERT_TRUE(rows.has_value());
    EXPECT_EQ(*rows, "1") << "type=place 行不应产生额外标签: " << *rows;
}

// 判据：index 与 header 两种写法对同一份数据结果**逐项相等**。
TEST_F(LoaderE2ETest, LabelByIndexEqualsLabelByHeader) {
    writeFile("by_header.csv", "id|type\n1|city\n2|country\n");
    writeFile("by_index.csv", "id|type\n1|city\n2|country\n");
    writeSchema(R"({
      "labels": {
        "A": [ { "file": "by_header.csv", "pk": "id", "columns": { "id": "INT64", "type": "STRING" },
                 "label": [ { "header": "type", "case": "capitalize" } ] } ],
        "B": [ { "file": "by_index.csv", "pk": "id", "columns": { "id": "INT64", "type": "STRING" },
                 "label": [ { "index": 1, "case": "capitalize" } ] } ]
      },
      "relationships": {}
    })");
    runLoad();

    for (const char* lab : {"City", "Country"}) {
        auto a = querySingle(std::string("MATCH (n:A:") + lab + ") RETURN count(n)");
        auto b = querySingle(std::string("MATCH (n:B:") + lab + ") RETURN count(n)");
        ASSERT_TRUE(a.has_value()) << lab;
        ASSERT_TRUE(b.has_value()) << lab;
        EXPECT_EQ(*a, *b) << lab << " 的 header 与 index 写法结果不一致";
    }
}

// 判据：同名表头必须能出两个不同标签（neo4j :LABEL|:LABEL 惯例），
// 且只用 header 时应报错提示改 index。
TEST_F(LoaderE2ETest, DuplicateHeaderLabelsRequireIndex) {
    writeFile("p.csv", "id|:LABEL|:LABEL\n1|Comment|Message\n");
    writeSchema(R"({
      "labels": { "Comment": [ { "file": "p.csv", "pk": "id",
                                 "columns": { "id": "INT64" },
                                 "label": [ { "index": 1 }, { "index": 2 } ] } ] },
      "relationships": {}
    })");
    auto summary = runLoad();
    EXPECT_NE(summary.find("vertices=1"), std::string::npos) << summary;

    auto n = querySingle("MATCH (n:Comment:Message) RETURN count(n)");
    ASSERT_TRUE(n.has_value()) << "两个 :LABEL 列未同时生效";
    EXPECT_EQ(*n, "1");

    // 只用 header 指向重名列 → 报错并提示用 index
    writeFile("q.csv", "id|:LABEL|:LABEL\n1|X|Y\n");
    writeSchema(R"({
      "labels": { "Comment": [ { "file": "q.csv", "pk": "id",
                                 "columns": { "id": "INT64" },
                                 "label": [ { "header": ":LABEL" } ] } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected duplicate header without index to be rejected";
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("appears 2 times"), std::string::npos) << msg;
        EXPECT_NE(msg.find("index"), std::string::npos) << msg;
    }
}

// 判据：旧的对象写法被明确拒绝，且报错里带上改法。
TEST_F(LoaderE2ETest, ObjectLabelFormIsRejectedWithMigrationHint) {
    writeFile("p.csv", "id|type\n1|city\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "type": "STRING" },
                               "label": { "header": "type", "case": "capitalize" } } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected object form of 'label' to be rejected";
    } catch (const std::exception& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("must be an array"), std::string::npos) << msg;
        EXPECT_NE(msg.find("Write it as"), std::string::npos) << msg;
    }
}

// 判据：元素内 derived 与 header 同时给出必须报错（旧写法是静默取 derived）。
TEST_F(LoaderE2ETest, DerivedAndHeaderTogetherAreRejected) {
    writeFile("p.csv", "id|type\n1|city\n");
    writeSchema(R"({
      "labels": { "Place": [ { "file": "p.csv", "pk": "id",
                               "columns": { "id": "INT64", "type": "STRING" },
                               "label": [ { "derived": ["City"], "header": "type" } ] } ] },
      "relationships": {}
    })");
    try {
        loadConfig();
        FAIL() << "expected derived+header combination to be rejected";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("mutually exclusive"), std::string::npos) << e.what();
    }
}

TEST_F(LoaderE2ETest, ReportsUndeclaredCsvFiles) {
    writeFile("a.csv", "id\n1\n");
    writeFile("extra.csv", "id\n1\n");
    writeSchema(R"({
      "labels": { "thing": [ { "file": "a.csv", "pk": "id", "columns": { "id": "INT64" } } ] },
      "relationships": {}
    })");
    auto config = loadConfig();
    auto undeclared = loader::findUndeclaredCsvFiles(config);
    ASSERT_EQ(undeclared.size(), 1u);
    EXPECT_EQ(undeclared[0], "extra.csv");
}

// ==================== CSV 读取能力（BOM / 引号 / 分隔符） ====================

TEST_F(LoaderE2ETest, HandlesBomAndQuotedFields) {
    writeFile("p.csv", "\xEF\xBB\xBFid|name|note\n1|\"a|b\"|\"say \"\"hi\"\"\"\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING", "note": "STRING" } } ] },
      "relationships": {}
    })");
    auto summary = runLoad();
    EXPECT_NE(summary.find("vertices=1"), std::string::npos) << summary;

    auto name = querySingle("MATCH (p:person) RETURN p.name");
    ASSERT_TRUE(name.has_value());
    EXPECT_NE(name->find("a|b"), std::string::npos) << *name;
}

// 分隔符现在来自 schema（不再有 --delimiter）
TEST_F(LoaderE2ETest, HandlesCommaDelimiterFromSchema) {
    writeFile("p.csv", "id,name\n1,alice\n");
    writeSchema(R"({
      "delimiter": ",",
      "labels": { "person": [ { "file": "p.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": {}
    })");
    auto config = loadConfig();
    loader::createLabels(*client_, config);
    loader::createPrimaryKeyIndexes(*client_, config);
    std::vector<shell::EuGraphRpcClient*> clients{client_.get()};
    auto [w, d] = loader::loadVertices(clients, config, 100, 1);
    EXPECT_EQ(w, 1);
    EXPECT_EQ(d, 0);
    auto name = querySingle("MATCH (p:person) RETURN p.name");
    ASSERT_TRUE(name.has_value());
    EXPECT_NE(name->find("alice"), std::string::npos) << *name;
}

} // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    folly::Init init(&argc, &argv, /*removeFlags=*/false);
    return RUN_ALL_TESTS();
}
