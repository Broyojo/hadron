# Electron RegExp replacement regression

Use the unmodified Windows x64 Electron 42.7.0 distribution. Copy these three
files into its `resources/app/` directory, then run `electron.exe` through
`scripts/play 0 /absolute/path/to/electron.exe` with an isolated prefix.

Read `C:\hadron-regexp-replace-result.json` in that prefix. It is overwritten
at startup and reports `PASS` after 10,000 replacements, or the first incorrect
replacement and its iteration. Do not add `--jitless` or RegExp interpretation
flags: they would bypass the path this test exercises. No window is required.

The Windows C companion `tests/win/host-page-smc.c` checks the underlying code
tracking directly, independently of Electron's allocation and tiering choices.
