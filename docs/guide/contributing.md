# Contributing

Hadron is a set of upstream projects (Wine, FEX, Mesa's KosmicKrisp and Zink, DXMT, mtld3d,
vkd3d-proton, NotProton) with patches, plus a small amount of its own code. Help of every size is
welcome.

## Without writing code

- **Report how games run** on [HadronDB](https://hadrondb.com), which collects how games run for
  players.
- **Report a game that fails** as a [GitHub issue](https://github.com/Broyojo/hadron/issues), with
  a report file ([how](./troubleshooting#save-a-report)). A game's report is what anyone needs to
  start on it.
- **Ask and answer questions** in [Discussions](https://github.com/Broyojo/hadron/discussions) or on
  the [Discord](https://discord.gg/ShNTcxWjKN).
- **Improve this guide.** Every page has an *Edit this page on GitHub* link at the bottom. The pages
  are Markdown files in [`docs/guide`](https://github.com/Broyojo/hadron/tree/main/docs/guide).

## Changing Hadron

These live in the repository and are written for people who change Hadron:

- [CONTRIBUTING.md](https://github.com/Broyojo/hadron/blob/main/CONTRIBUTING.md): where a change
  goes, how patches to upstream projects are kept, working without an Apple Developer account,
  using a coding agent, and the upstream projects' own rules.
- [AGENTS.md](https://github.com/Broyojo/hadron/blob/main/AGENTS.md): what building Hadron taught
  about working on it safely, for a person or their coding agent.
- [README.md](https://github.com/Broyojo/hadron#building): how to build Hadron.
- [docs/architecture.md](https://github.com/Broyojo/hadron/blob/main/docs/architecture.md): the
  macOS constraints behind the design.
- [docs/findings.md](https://github.com/Broyojo/hadron/blob/main/docs/findings.md): the numbered
  log of what was measured and fixed, with the open problems marked.
- [docs/conformance.md](https://github.com/Broyojo/hadron/blob/main/docs/conformance.md): results
  of the Khronos conformance suites on the Vulkan and OpenGL drivers.
- [docs/roadmap.md](https://github.com/Broyojo/hadron/blob/main/docs/roadmap.md): the order of work.

Two rules shape every fix: fix the shared layer, not the game, and drivers report only what they
implement.
