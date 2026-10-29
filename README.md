# Omaread

A dead-simple Markdown reader for [Omarchy](https://omarchy.org), built with
Qt Quick and C++. It shows a file the way GitHub shows it, follows the Omarchy
theme and system dark/light mode, and keeps up with the file as it changes. It
is modelled on [Omawrite](https://github.com/omacom/omawrite), and the two work
as a pair: write in one, read in the other.

Omaread is a community app. It is not made by, or affiliated with, the Omarchy
team.

<img width="1960" height="1200" alt="Omaread showing a project README in GitHub's light colours, with its contents beside the page" src="docs/omaread-light.png" />
<img width="1960" height="1200" alt="The same README in GitHub's dark colours" src="docs/omaread-dark.png" />

## Install

On Omarchy or Arch Linux, with `git` and `base-devel` installed:

```sh
git clone https://github.com/Frestina/omaread && cd omaread && ./bin/install
```

This builds a package and installs it through pacman, which asks for your
password, along with what Omaread needs. Run it again after a `git pull` to
update.

To make Omaread what opens `.md` files:

```sh
xdg-mime default omaread.desktop text/markdown text/x-markdown
```

Math and Mermaid diagrams are optional. Without them, a formula or diagram
shows as its source with a line under it naming what to install:

```sh
sudo pacman -S nodejs mathjax    # math
sudo pacman -S mermaid-cli       # Mermaid diagrams
```

To uninstall:

```sh
sudo pacman -R omaread
```

## Features

- **GitHub's Markdown.** Files are parsed by
  [cmark-gfm](https://github.com/github/cmark-gfm), the parser github.com
  itself uses, with the same extensions: tables, task lists, strikethrough,
  autolinks and footnotes, plus GitHub's alerts (`> [!NOTE]`, `> [!TIP]`,
  `> [!IMPORTANT]`, `> [!WARNING]`, `> [!CAUTION]`) and emoji shortcodes, so
  `:tada:` shows 🎉. Headings, quotes, code panels and tables use github.com's
  sizes and spacing.
- **Code** that names its language is highlighted by
  [KSyntaxHighlighting](https://invent.kde.org/frameworks/syntax-highlighting),
  which knows some three hundred, in GitHub's colours or the Omarchy theme's.
- **Math**, `$…$` in a line and `$$…$$` or a `` ```math `` block on its own,
  is drawn by [MathJax](https://www.mathjax.org), the engine GitHub uses.
  Arch's MathJax leaves out the fonts for `\mathbb`, `\mathcal` and the like,
  so formulas using them show as TeX.
- **Mermaid** `` ```mermaid `` blocks are drawn as diagrams by Mermaid's own
  `mmdc`, in Mermaid's light and dark themes as on github.com, or in the
  Omarchy theme's colours.
- **READMEs read as they would in their repository.** Images and links
  resolve relative to the file, and a path starting with `/` from the root of
  its git repository. Badges and screenshots load from the web, and GIFs
  play. HTML such as `<p align="center">`, `<img width="200">`,
  `<div align="center">` and `<details>` is honoured; `<details>` folds as on
  GitHub.
- **Links.** A link to another Markdown file opens it in the same window, and
  `Alt+Left` comes back. Links to `#headings` and footnotes scroll there, and
  web and `mailto:` links open in your browser or mail app. See
  [Privacy and safety](#privacy-and-safety) for links to other files.
- **The Omarchy look.** Colours come from the current Omarchy theme and
  follow it when it changes, falling back to GitHub's own light and dark
  palettes when there is none. The rounded corners of the find bar, the
  shortcuts and the back-to-top button follow Hyprland's window rounding.
  Text follows the desktop text size (`omarchy display text size`).
- **Keeps up with the file.** Changes on disk show by themselves, and the page
  keeps your place: the block you are reading stays at the top of the window,
  even when text above it changes or images above it load.
- **Contents.** A panel beside the page lists the headings, marks the section
  you are reading, and glides to a heading on a click. It lays itself over the
  page when the window is narrow.
- **Find in the page**, ignoring case and accents, so `cafe` finds Café.
- **Print and Save as PDF**, laid out as on screen in GitHub's light colours.
- **Scrolling** as in a browser: a touchpad moves the page with your fingers,
  a mouse wheel glides, and once you are a window down, a button at the bottom
  right goes back to the top.
- **Omawrite.** `Ctrl+E` opens the file in Omawrite when it is installed;
  Omaread keeps showing the file and updates as you save.

## Shortcuts

`F1` shows them all in the window.

| Shortcut | Does |
| --- | --- |
| **File** | |
| `Ctrl+O` | Open a file (or drop one on the window) |
| `Ctrl+P` | Print |
| `Ctrl+Shift+S` | Save as PDF |
| `Ctrl+E` | Edit in Omawrite |
| `Ctrl+R` or `F5` | Reload |
| `Alt+Left` or `Backspace` | Go back |
| `Ctrl+W` or `Ctrl+Q` | Close the window |
| **Find** | |
| `Ctrl+F` | Find in page |
| `Enter`, `Ctrl+G` or `F3` | Next match |
| `Shift+Enter`, `Ctrl+Shift+G` or `Shift+F3` | Previous match |
| `Esc` | Close find |
| **Reading** | |
| `Down` or `j` | Scroll down |
| `Up` or `k` | Scroll up |
| `Space` or `Page Down` | Next screen |
| `Shift+Space` or `Page Up` | Previous screen |
| `Home` or `g` | Top |
| `End` or `G` | Bottom |
| `Ctrl+C` | Copy the selection |
| **View** | |
| `Ctrl+=` | Larger text |
| `Ctrl+-` | Smaller text |
| `Ctrl+0` | Default text size |
| `Ctrl+scroll` | Change the text size |
| `Ctrl+Shift+O` | Contents |
| `F11` or `Super+F` | Fullscreen |
| `F1`, `Ctrl+/` or `Ctrl+?` | Keyboard shortcuts |

## Command line

`omaread FILE` opens a Markdown file. `omaread` alone opens an empty window to
drop a file on. `omaread --help` and `omaread --version` answer in the terminal
without opening a window.

## Privacy and safety

Omaread has no telemetry and never checks for updates. It goes on the network
only for images a file links to, such as badges and screenshots in a README.
They load as soon as the file opens, so the sites they come from see your IP
address, as a browser opening the page would show it. Unlike github.com, there
is no proxy in between.

Clicking a link never runs anything. Web and `mailto:` links open in your
browser or mail app, and Markdown files open in Omaread. Pictures, PDFs and
plain-text files open in their viewer when both their name and their contents
say that is what they are. Any other file, and any folder, is shown in your
file manager instead, for you to open if you choose. Links to other kinds of
address, such as `steam://` or `vscode://`, are refused.

Scripts, styles and style attributes in a file's HTML are dropped, as on
GitHub. Math and diagrams are drawn on your computer, by `node` and `mmdc`,
only when you have installed them.

Omaread keeps its settings (text size, whether the contents show, window size)
in `~/.config/omaread`, and drawn formulas and diagrams in
`~/.cache/omaread/figures`, where they are cleared a month after they were
drawn.

## Development

Omaread needs Qt 6 (`qt6-base`, `qt6-declarative`, `qt6-svg`), `cmark-gfm`,
`syntax-highlighting` and `xdg-desktop-portal` with a portal backend; to
build, a C++17 compiler such as `gcc` and `make`.

```sh
./bin/build
./bin/test
./build/omaread README.md
```

`bin/test` runs the Qt test suite with the offscreen platform, so it does not
need a graphical session. `bin/install` builds the Arch package from the
working tree with `makepkg -fsi`.

`bin/snapshot` renders a file offscreen, to look at a change without opening
Omaread: a PNG of the window, or the printed pages as a PDF.

```sh
./bin/snapshot README.md /tmp/readme.png                 # in your Omarchy theme
./bin/snapshot --light --size 800x1200 README.md /tmp/readme.png
./bin/snapshot --dark --scroll 900 README.md /tmp/further-down.png
./bin/snapshot --outline README.md /tmp/with-contents.png
./bin/snapshot README.md /tmp/readme.pdf                 # as printed, on A4
```

QML warnings show in the terminal. `--light` and `--dark` use GitHub's own
colours instead of the Omarchy theme.

`bin/screenshots` remakes the two screenshots above from a made-up project in
`tools/demo/stargazer`. `tools/update-emoji` remakes the emoji shortcodes from
GitHub's list in [gemoji](https://github.com/github/gemoji).

## License

MIT. Parts are adapted from Omawrite, also MIT; see `LICENSE`.
