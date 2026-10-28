# csv.h

Vendored from `euleraph` (`/home/dodo/code/euleraph/src/utils/csv.h`).

- Original author: Ben Strasser <code@ben-strasser.net>
- License: BSD-3-Clause (see header in `csv.h`)
- Delimiter: configurable via `io::no_quote_escape<'|'>`

**当前接入范围**：`eugraph-loader` 只用它的 `io::LineReader` 做**预扫描行数统计**
（`src/program/loader/csv_reader.cpp`，无引号文件走这条快速路径；一旦出现引号字符或超长行，
回落到 loader 自己的精确计数，避免「引号内换行」把记录数数错）。实测 sf0.1 语料
61.6MB / 181 万记录：LineReader 计数 81ms，整文件解析 436ms（约 5.4×）。

`io::CSVReader` 仍不用于数据解析：它是**行式**解析（引号字段不能跨行）且列数必须编译期已知，
而 loader 需要「引号内换行」与任意列数（设计文档 §3 R2）；数据解析的瓶颈也不在解析本身
（在 server 端 WiredTiger）。
