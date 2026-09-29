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
        work_dir_ = "/tmp/eugraph_loader_e2e_" + std::to_string(getpid());
        std::filesystem::remove_all(work_dir_);
        std::filesystem::create_directories(work_dir_ + "/data");

        graph_manager_ = std::make_shared<GraphManager>();
        ASSERT_TRUE(graph_manager_->init(work_dir_ + "/db", 2, 2));

        graph_service_ = std::make_shared<service::GraphService>(*graph_manager_);
        handler_ = std::make_shared<service::thrift::EuGraphHandler>(*graph_service_);

        auto ts = std::make_shared<apache::thrift::ThriftServer>();
        ts->setAddress(folly::SocketAddress("::1", 0));
        ts->setInterface(handler_);
        ts->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
        ts->setMaxFinishedDebugPayloadsPerWorker(0);
        ts->setThreadManagerFromExecutor(ts->getIOThreadPool().get());
        server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(ts);

        client_ = std::make_unique<shell::EuGraphRpcClient>("::1", server_->getPort());
        ASSERT_TRUE(client_->connect());
    }

    void TearDown() override {
        client_.reset();
        (void)server_.release(); // 避免在析构里 join 线程导致偶发挂住（沿用既有测试做法）
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

    loader::SchemaConfig loadConfig(loader::CsvDialect dialect = loader::CsvDialect{}) {
        return loader::loadSchemaConfig(std::filesystem::path(work_dir_) / "schema.json", work_dir_ + "/data", dialect);
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
        loader::CsvDialect dialect;
        loader::createLabels(*client_, config);
        loader::createEdgeLabels(*client_, config);
        loader::createPrimaryKeyIndexes(*client_, config);
        std::vector<shell::EuGraphRpcClient*> clients{client_.get()};
        auto [vw, vd] = loader::loadVertices(clients, config, dialect, 100, 1);
        auto [ew, es] = loader::loadEdges(clients, config, dialect, 100, 1);
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
                               "label": { "header": "type" } } ],
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
                                      "label": { "header": "type" } } ] },
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
    auto config = loader::loadSchemaConfig(std::filesystem::path(work_dir_) / "schema.json", work_dir_ + "/data",
                                           loader::CsvDialect{}, /*strict_types=*/false);
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

TEST_F(LoaderE2ETest, CliOverridesApplyPkAndTypes) {
    writeFile("p.csv", "id|name|score\n1|alice|7\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": {}
    })");
    // --pk 'person=id' + --types 'person=score:INT64' 应把 score 补成显式 INT64（strict 下也通过）
    loader::CliOverrides ov;
    ov.pk["person"] = {"id"};
    ov.types["person"]["score"] = "INT64";
    auto config = loader::loadSchemaConfig(std::filesystem::path(work_dir_) / "schema.json", work_dir_ + "/data",
                                           loader::CsvDialect{}, /*strict_types=*/true, &ov);
    const auto& merged = config.merged_properties.at("person");
    bool found_score = false;
    for (const auto& c : merged) {
        if (c.name == "score") {
            found_score = true;
            EXPECT_EQ(c.type, loader::CsvColumnType::INT64);
            EXPECT_TRUE(c.type_declared);
        }
    }
    EXPECT_TRUE(found_score);

    // --pk 指向未声明属性 → 报错
    loader::CliOverrides bad;
    bad.pk["person"] = {"nope"};
    EXPECT_THROW(loader::loadSchemaConfig(std::filesystem::path(work_dir_) / "schema.json", work_dir_ + "/data",
                                          loader::CsvDialect{}, /*strict_types=*/true, &bad),
                 std::runtime_error);
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

TEST_F(LoaderE2ETest, HandlesCommaDelimiter) {
    writeFile("p.csv", "id,name\n1,alice\n");
    writeSchema(R"({
      "labels": { "person": [ { "file": "p.csv", "pk": "id",
                                "columns": { "id": "INT64", "name": "STRING" } } ] },
      "relationships": {}
    })");
    loader::CsvDialect dialect;
    dialect.delimiter = ',';
    auto config = loadConfig(dialect);
    loader::createLabels(*client_, config);
    loader::createPrimaryKeyIndexes(*client_, config);
    std::vector<shell::EuGraphRpcClient*> clients{client_.get()};
    auto [w, d] = loader::loadVertices(clients, config, dialect, 100, 1);
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
