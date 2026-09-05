# csv.h

Vendored from `euleraph` (`/home/dodo/code/euleraph/src/utils/csv.h`).

- Original author: Ben Strasser <code@ben-strasser.net>
- License: BSD-3-Clause (see header in `csv.h`)
- Delimiter: configurable via `io::no_quote_escape<'|'>`

Kept as a fast CSV reader option for future loader work. It is not currently
wired into `eugraph-loader` because local sf0.1 benchmarks showed the existing
`std::getline('|')` path is not the bottleneck (server WiredTiger is).
