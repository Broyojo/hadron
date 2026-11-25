import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitepress'

// The user guide at docs.hadrondb.com. The pages are Markdown in docs/guide, next to the rest of
// the documentation; this folder holds only the site's build setup.
const guide = fileURLToPath(new URL('../../docs/guide/', import.meta.url))
const site = fileURLToPath(new URL('../package.json', import.meta.url))

export default defineConfig({
  srcDir: '../docs/guide',
  vite: {
    plugins: [
      {
        // Pages compiled from docs/guide import Vue, and no folder above docs/guide has a
        // node_modules: look packages up from this folder instead.
        name: 'hadron-resolve-from-site',
        enforce: 'pre',
        resolveId(source, importer, options) {
          if (!importer?.startsWith(guide) || /^[./\0]/.test(source)) return null
          return this.resolve(source, site, { ...options, skipSelf: true })
        },
      },
    ],
  },
  title: 'Hadron',
  description: 'Play Windows games from the Steam library of the Mac Steam client, on Apple silicon.',
  lang: 'en-US',
  cleanUrls: true,
  lastUpdated: true,
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/hadron-icon-small.svg' }],
  ],
  themeConfig: {
    logo: { light: '/hadron-mark-light.svg', dark: '/hadron-mark.svg', alt: '' },
    nav: [
      { text: 'Guide', link: '/introduction' },
      { text: 'Graphics', link: '/graphics/' },
      { text: 'Game reports', link: 'https://hadrondb.com' },
      { text: 'Releases', link: 'https://github.com/Broyojo/hadron/releases' },
    ],
    sidebar: [
      {
        text: 'Getting started',
        items: [
          { text: 'Introduction', link: '/introduction' },
          { text: 'Install', link: '/install' },
          { text: 'Playing Steam games', link: '/steam-games' },
          { text: 'Windows programs outside Steam', link: '/windows-programs' },
        ],
      },
      {
        text: 'Using Hadron',
        items: [
          { text: 'Settings', link: '/settings' },
          { text: 'Performance and display', link: '/performance' },
          { text: 'Troubleshooting', link: '/troubleshooting' },
          { text: 'FAQ', link: '/faq' },
          { text: 'Uninstall', link: '/uninstall' },
        ],
      },
      {
        text: 'Graphics',
        items: [
          { text: 'What runs where', link: '/graphics/' },
          { text: 'Direct3D 9', link: '/graphics/direct3d-9' },
          { text: 'Direct3D 10 and 11', link: '/graphics/direct3d-11' },
          { text: 'Direct3D 12', link: '/graphics/direct3d-12' },
          { text: 'Vulkan', link: '/graphics/vulkan' },
          { text: 'OpenGL', link: '/graphics/opengl' },
        ],
      },
      {
        text: 'Project',
        items: [
          { text: 'Contributing', link: '/contributing' },
        ],
      },
    ],
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Broyojo/hadron' },
      { icon: 'discord', link: 'https://discord.gg/ShNTcxWjKN' },
    ],
    editLink: {
      pattern: 'https://github.com/Broyojo/hadron/edit/main/docs/guide/:path',
      text: 'Edit this page on GitHub',
    },
    search: { provider: 'local' },
    outline: { level: [2, 3] },
    footer: {
      message: 'Hadron is not affiliated with or endorsed by Valve, Apple or CodeWeavers. BSD 3-Clause for Hadron\'s own code.',
    },
  },
})
