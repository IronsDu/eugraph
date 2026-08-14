# 构建与运行指南

> [当前实现] 参见 [README.md](../README.md) 返回文档导航

## 1. 前置条件

- C++20 编译器（GCC 13+ / Clang 15+）
- CMake 3.25+
- Git

## 2. 获取代码

```bash
git clone --recurse-submodules <repo-url>
# 如果已克隆但未初始化子模块：
git submodule update --init --recursive
```

项目使用两个 git submodule：
- `vcpkg/` — 依赖管理（C++ 包管理器）
- `third_party/wiredtiger/` — WiredTiger 存储引擎（mongodb-8.2.7 分支）

## 3. 构建

所有构建均使用项目自带的 vcpkg 子模块，无需单独安装 vcpkg。

### 3.1 首次引导 vcpkg

```bash
./vcpkg/bootstrap-vcpkg.sh
```

### 3.2 使用 CMake Presets（推荐）

项目在 `CMakePresets.json` 中预配置了以下 presets：

| Preset | 说明 |
|--------|------|
| `debug` | Debug 构建（开发调试） |
| `release` | Release 构建 |
| `relwithdebinfo` | Release with Debug Info |
| `coverage` | Debug + 代码覆盖率（gcovr） |
| `clang-debug` | Clang Debug |
| `clang-release` | Clang Release |

```bash
# 配置 + 构建（以 release 为例）
cmake --preset=release
cmake --build --preset=release

# 其他 preset 同理
cmake --preset=debug
cmake --build --preset=debug
```

所有 preset 自动使用 `${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake` 作为 toolchain，无需手动指定。

### 3.3 手动 CMake 配置

如果不使用 preset：

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 4. Sanitizer 构建

### 4.1 AddressSanitizer (ASan)

```bash
cmake -B build-asan -S . \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=address"
cmake --build build-asan

# 运行测试
cd build-asan && ctest --output-on-failure
```

环境变量建议：
```bash
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
```

### 4.2 UndefinedBehaviorSanitizer (UBSan)

```bash
cmake -B build-ubsan -S . \
  -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=undefined -g" \
  -DCMAKE_C_FLAGS="-fsanitize=undefined -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined" \
  -DCMAKE_MODULE_LINKER_FLAGS="-fsanitize=undefined"
cmake --build build-ubsan

# 运行测试
cd build-ubsan && ctest --output-on-failure
```

环境变量建议：
```bash
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
```

## 5. 运行测试

### 5.1 使用 CMake Presets

```bash
# 运行全部测试（对应 preset）
ctest --preset=release --verbose
ctest --preset=debug --verbose
```

### 5.2 手动运行单个测试

```bash
./build/release/eugraph_tests          # 图存储层测试
./build/release/metadata_tests          # 元数据服务测试
./build/release/query_executor_tests    # 查询执行器测试
./build/release/cypher_parser_tests     # Cypher 解析器测试
./build/release/logical_plan_tests      # 逻辑计划测试
./build/release/index_tests             # 二级索引测试
./build/release/loader_tests            # CSV Loader 测试
```

## 6. 启动服务

详细使用说明见 [program/usage/](../program/usage/)。

```bash
# 服务端
./build/release/eugraph-server -d /path/to/data

# 客户端 Shell
./build/release/eugraph-shell --host ::1 --port 9090

# CSV 批量导入
./build/release/eugraph-loader --data-dir /path/to/csv --host 127.0.0.1 --port 9090
```

## 7. Thrift 代码生成

当修改了 `proto/eugraph.thrift` 后，需要重新生成 C++ 代码。

### 生成命令

```bash
# thrift1 编译器位置（由 vcpkg fbthrift 包安装）
THRIFT1=./vcpkg/packages/fbthrift_x64-linux/tools/fbthrift/thrift1

# 生成到 src/service/thrift/gen-cpp2/
# :include_prefix=./service/thrift 让生成代码内部互相引用时使用
# "./service/thrift/gen-cpp2/..." 路径，与物理路径一致，直接通过 src/
# 这条标准 include 路径就能解析。应用代码则用 #include "service/thrift/gen-cpp2/..."。
# 不加这个选项的话，thrift1 会用输入路径的目录部分（"proto/"）作为前缀，
# 生成出 "proto/gen-cpp2/..." 这种与物理路径不一致的写法。
$THRIFT1 --gen mstch_cpp2:include_prefix=./service/thrift -o src/service/thrift/ proto/eugraph.thrift
```

### 验证

```bash
# 检查生成的 include 路径没有 proto/ 前缀
grep -r '"/gen-cpp2/\|"proto/gen-cpp2/' src/service/thrift/gen-cpp2/ && echo "ERROR: include 路径异常" || echo "OK"
```

## 8. ANTLR 代码生成

Cypher 解析器的 ANTLR4 生成代码（`src/query/parser/generated/grammar/Cypher*.{h,cpp}`）预生成后提交仓库，CMake 编译时直接使用，不依赖 Java/JAR。

修改 `grammar/CypherLexer.g4` 或 `grammar/CypherParser.g4` 后需要重新生成：

```bash
./scripts/gen-grammar.sh
```

### 关键细节

脚本内部做了两件容易踩坑的事：

1. **`cd grammar/` + 裸文件名输入** — 让 ANTLR 在生成的 `// Generated from <name>.g4` 注释里只写裸文件名，不带任何路径前缀。如果传 `grammar/CypherLexer.g4` 或绝对路径，注释里就会出现 `"grammar/CypherLexer.g4"` 或 `"/home/user/.../CypherLexer.g4"`。
2. **生成 parser 时 `-lib` 指向输出目录** — 让 ANTLR 找到上一步生成的 `CypherLexer.tokens`。没有 `-lib` 的话，ANTLR 会把 lexer 里已定义的 `EXPLAIN`、`COLONCOLON` 当成"隐式定义"追加到 parser token 表末尾，导致后续所有 token ID 错位（肉眼难发现，但会破坏序列化兼容性）。

### 验证

```bash
# 注释里不应出现路径前缀
grep "^// Generated from" src/query/parser/generated/grammar/Cypher*.{h,cpp}
# 期望输出（每文件一行）：
#   // Generated from CypherLexer.g4 by ANTLR 4.13.2
#   // Generated from CypherParser.g4 by ANTLR 4.13.2
```

脚本是幂等的，重复运行不应产生 diff。

## 9. 代码格式化

```bash
# 检查格式（不修改）
./scripts/check-format.sh

# 自动格式化
./scripts/check-format.sh --fix
```

要求 clang-format v18。脚本支持通过 Docker 容器运行（本地无 clang-format 18 时自动使用容器）。

## 10. CI

项目使用 GitHub Actions，配置在 `.github/workflows/`：

- **ci.yml** — GCC 构建 + 测试、Clang 构建 + 测试、代码覆盖率
- **code-quality.yml** — clang-format 检查、ASan、UBSan、clang-tidy、-Werror 构建

所有 CI 作业使用 vcpkg 二进制缓存，首次构建后后续构建会复用缓存加速。
