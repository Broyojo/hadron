---
layout: home
title: Hadron
titleTemplate: Windows games from your Steam library, on Apple silicon

hero:
  name: Hadron
  text: Windows games from your Mac's Steam library
  tagline: An open-source compatibility tool for Steam on Apple silicon Macs. It is to the Mac what Proton is to Linux.
  image:
    light: /hadron-light.svg
    dark: /hadron-dark.svg
    alt: Hadron
  actions:
    - theme: brand
      text: Install
      link: /install
    - theme: alt
      text: What Hadron is
      link: /introduction
    - theme: alt
      text: How games run
      link: https://hadrondb.com

features:
  - title: Plays from Steam for Mac
    details: Install and start Windows games from the native Mac Steam client, as you would a Mac game. Sign-in, friends and cloud saves come from the Mac client.
    link: /steam-games
    linkText: Playing Steam games
  - title: No Rosetta
    details: Wine is built natively for arm64, FEX translates the game's x86 code, and Direct3D, Vulkan and OpenGL are translated to Metal.
    link: /graphics/
    linkText: What runs where
  - title: Programs outside Steam too
    details: Open any Windows .exe with Hadron, or run it with the hadron command.
    link: /windows-programs
    linkText: Windows programs
---

::: warning Early development
A handful of games play well and many do not start yet. [The game list](https://github.com/Broyojo/hadron/blob/main/docs/test-games.md) says which have been tried, and [HadronDB](https://hadrondb.com) is where reports of how games run are being collected.
:::
