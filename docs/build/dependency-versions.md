# 依赖版本（vcpkg）

> [当前实现 + 设计规划] 参见 [README.md](../README.md) 返回文档导航

本文档记录 EuGraph 的 vcpkg 依赖版本固定方式、直接依赖版本，以及 CI 缓存的滚动策略。

## 一、版本唯一来源：vcpkg baseline

`vcpkg.json` 只声明直接依赖，不写死每个 port 的版本；版本由 **`builtin-baseline`** 固定：

```json
{
  "builtin-baseline": "2cf2bcc60add50f79b2c418487d9cd1b6c7c1fec"
}
```

对应的 vcpkg 仓库与 baseline：

- vcpkg 仓库：<https://github.com/microsoft/vcpkg>
- baseline commit：<https://github.com/microsoft/vcpkg/tree/2cf2bcc60add50f79b2c418487d9cd1b6c7c1fec>
- 本仓库的 vcpkg submodule 也固定在同一 commit：

```text
2cf2bcc60add50f79b2c418487d9cd1b6c7c1fec vcpkg
```

baseline 的作用：vcpkg 按该 commit 里的 `ports/<name>/vcpkg.json` 解析**每个 port（含传递依赖）的
version + port-version**。因此“没有在 `vcpkg.json` 里逐个写版本号”不等于版本漂移；
只要 baseline 不变，依赖版本就是确定的。

> 历史坑：此前 `vcpkg-configuration.json` 里有一个空 baseline 的
> `default-registry`，会覆盖 `vcpkg.json` 的 `builtin-baseline`。该文件已删除。

### 为什么不用 `overrides`

`overrides` 可以在 `vcpkg.json` 里逐个写精确版本，但会触发 vcpkg versioning 去 checkout
版本数据库里的历史 port tree。CI 的 vcpkg submodule 是 shallow clone，缺少这些 git 对象，
会直接失败：

```text
fatal: failed to unpack tree object ...
note: vcpkg was cloned as a shallow repository.
```

如果以后确实需要在 manifest 里显式写死版本，需要同时在所有 CI job 里
`git -C vcpkg fetch --unshallow`（或 fetch 所需历史），否则无法解析 overrides。

### libunwind overlay

内置 `libunwind` port 使用的 GitHub 自动归档 tarball 内容/hash 发生了变化，CI 报
`unexpected hash`。仓库内新增 [ports/libunwind](../../ports/libunwind) overlay，
改用 GitHub Release 的固定 asset + SHA512：

```text
https://github.com/libunwind/libunwind/releases/download/v1.8.3/libunwind-1.8.3.tar.gz
```

这是**源地址**的固定，不改变版本号（仍是 1.8.3）。

## 二、直接依赖版本

以下版本由当前 baseline 解析得到，并与本地 `build/release/vcpkg_installed` 的实际安装一致
（`vcpkg list` / `vcpkg_installed/vcpkg/status` 可复核）：

| 依赖 | 版本 | 说明 |
|---|---|---|
| antlr4 | 4.13.2 | Cypher parser |
| args | 6.4.7 | CLI |
| rocksdb | 10.7.5 | 存储依赖 |
| gtest | 1.17.0 | 单测 |
| spdlog | 1.16.0 | 日志 |
| lz4 | 1.10.0 | 压缩 |
| zlib | 1.3.1 | 压缩 |
| zstd | 1.5.7 | 压缩 |
| folly | 2025.05.19.00 | 协程/运行时 |
| fbthrift | 2025.05.19.00 | RPC/序列化 |
| drogon | 1.9.11 | HTTP/Web |
| nlohmann-json | 3.12.0 | JSON |
| benchmark | 1.9.4 | 微基准 |

### 主要传递依赖（同由 baseline 固定）

| 依赖 | 版本 |
|---|---|
| boost-* | 1.90.0 |
| openssl | 3.6.0 |
| fmt | 12.1.0 |
| fizz / wangle / mvfst | 2025.05.19.00 |
| libunwind | 1.8.3（overlay 源） |
| glog | 0.7.1 |
| gflags | 2.3.0 |
| libevent | 2.1.12+20230128 |
| trantor | 1.5.24 |
| rocksdb 传递：xxhash / lz4 / zstd / zlib | 0.8.3 / 1.10.0 / 1.5.7 / 1.3.1 |

完整列表以本地安装为准：

```bash
# 直接看已安装版本
./vcpkg/vcpkg list --x-install-root=build/release/vcpkg_installed

# 或解析 status
grep -E '^(Package|Version):' build/release/vcpkg_installed/vcpkg/status
```

## 三、升级流程

1. 更新 vcpkg submodule 到目标 commit（或修改 `builtin-baseline` 为该 commit）；
2. `vcpkg-configuration.json` 保持不存在（不要重新加空 baseline）；
3. 跑：

   ```bash
   cmake --preset release
   ```

   让 vcpkg 重新解析/安装；
4. 用 `vcpkg list` 核对直接依赖版本，更新本文档表格；
5. 若 CI 的 binary cache ABI 变化，首次会重建依赖；缓存键包含 `vcpkg.json`、
   `ports/**`、`CMakePresets.json`，不会误用旧 ABI 的包。

## 四、CI ccache 的“周滚动”是什么

**不是“缓存只能活一周、每周强制全量重建”。**

`actions/cache` 的条目是**不可变**的：同一个 key 一旦命中，本次 job 结束时不会再写回新内容。
如果 ccache 用固定 key，缓存会永远停留在第一次的内容；如果用 per-SHA key，则每个 commit
都新增一个 ~1GB 条目，很快把 GitHub 的 10GB repo quota 挤爆，导致互相淘汰、缓存长期冷。

当前策略：

```yaml
key: ${{ runner.os }}-ccache-gcc-${{ steps.ccache_week.outputs.week }}
restore-keys: ${{ runner.os }}-ccache-gcc-
```

- 同一周内：key 固定，命中后不重复保存，同一周的构建复用同一份 ccache；
- 跨周：新 key 主键 miss，但 `restore-keys` 会恢复**上一周（或最近）**的 ccache；
  本次构建仍然大量命中旧对象，只在 job 结束时把新对象保存到新一周的 key；
- 旧周条目不会立刻删除，由 GitHub 按 10GB quota / LRU 淘汰；
- 首次没有历史缓存、缓存被淘汰、或依赖 ABI 变化时，才会出现明显的冷构建。

因此周滚动是**缓存条目的轮换周期**，不是“每周必须重新编译”；
它用少量的一次性 miss 换取缓存可更新、quota 不被 per-SHA 条目打爆。

vcpkg binary archive 的 key 不含周，而是包含依赖定义：

```yaml
key: ${{ runner.os }}-vcpkg-archives-gcc-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'ports/**', 'CMakePresets.json') }}
```

依赖定义变化（baseline/overlay）才会让 vcpkg 缓存失效；ABI 不符时 vcpkg 会拒绝旧包并重建。
