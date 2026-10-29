#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "cli.h"
#include "codehighlighter.h"
#include "figures.h"
#include "findhighlighter.h"
#include "markdownrenderer.h"
#include "printdocument.h"

namespace {
const QUrl baseUrl = QUrl::fromLocalFile(QStringLiteral("/repo/docs/"));
const QUrl rootUrl = QUrl::fromLocalFile(QStringLiteral("/repo/"));

RenderedMarkdown render(const char *markdown) {
    return MarkdownRenderer::render(QByteArray(markdown), baseUrl, rootUrl);
}

QVariantMap blockAt(const RenderedMarkdown &rendered, int index) {
    return rendered.blocks.at(index).toMap();
}

QVariantMap container(const QVariantMap &block, int index) {
    return block.value("containers").toList().at(index).toMap();
}

// Omaread's own window, offscreen, showing what its backend shows.
struct Window {
    Backend backend;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;

    Window() {
        // Whatever a run before left remembered.
        backend.setOutlineOpen(false);
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (window) {
            window->resize(1000, 800);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
    }

    template <typename T = QQuickItem>
    T *find(const char *name) const {
        return window->findChild<T *>(QLatin1String(name));
    }

    // An item a view made, such as a list's entry, which only the items it
    // sits in know of.
    QQuickItem *findShown(const char *name, QQuickItem *in = nullptr) const {
        in = in ? in : window->contentItem();
        for (QQuickItem *child : in->childItems()) {
            if (child->objectName() == QLatin1String(name))
                return child;
            if (QQuickItem *found = findShown(name, child))
                return found;
        }
        return nullptr;
    }

    void click(QQuickItem *item) const {
        QVERIFY(item);
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    }
};
}

class OmareadTest : public QObject {
    Q_OBJECT

private slots:
    // Figures are drawn into a cache of the tests' own.
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Omaread's style, for every window the tests show.
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    }

    void headingsGetGitHubAnchors() {
        const RenderedMarkdown rendered = render("# Hello, *World*!\n\n## Hello, World\n\n### `make` & ünïcode\n");
        QCOMPARE(blockAt(rendered, 0).value("anchor").toString(), QStringLiteral("hello-world"));
        QCOMPARE(blockAt(rendered, 1).value("anchor").toString(), QStringLiteral("hello-world-1"));
        QCOMPARE(blockAt(rendered, 2).value("anchor").toString(), QStringLiteral("make--ünïcode"));
        QCOMPARE(rendered.anchors.value("hello-world-1"), 1);
        QCOMPARE(blockAt(rendered, 0).value("html").toString(), QStringLiteral("Hello, <i>World</i>!"));
    }

    void inlineMarkupBecomesRichText() {
        const RenderedMarkdown rendered = render("**b** ~~s~~ `a<b` [l](x.md) <kbd>K</kbd>\nnext\n");
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("<b>b</b> <s>s</s> <code>a&lt;b</code> <a href=\"x.md\">l</a> "
                                "<kbd>K</kbd> next"));
    }

    void alertsBecomeCallouts() {
        const RenderedMarkdown rendered = render("> [!warning]\n> Mind the *gap*.\n");
        QCOMPARE(rendered.blocks.size(), 2);
        QCOMPARE(blockAt(rendered, 0).value("type").toString(), QStringLiteral("calloutTitle"));
        QCOMPARE(blockAt(rendered, 0).value("label").toString(), QStringLiteral("Warning"));
        QCOMPARE(blockAt(rendered, 1).value("html").toString(), QStringLiteral("Mind the <i>gap</i>."));
        QCOMPARE(container(blockAt(rendered, 1), 0).value("callout").toString(), QStringLiteral("warning"));
        QCOMPARE(blockAt(rendered, 1).value("joinPrevious").toInt(), 1);
    }

    void markerMidLineStaysAQuote() {
        const RenderedMarkdown rendered = render("> [!NOTE] inline\n\n> [!NOTE]\n");
        QCOMPARE(rendered.blocks.size(), 2);
        QCOMPARE(blockAt(rendered, 0).value("html").toString(), QStringLiteral("[!NOTE] inline"));
        QCOMPARE(container(blockAt(rendered, 0), 0).value("callout").toString(), QString());
        QCOMPARE(blockAt(rendered, 1).value("html").toString(), QStringLiteral("[!NOTE]"));
    }

    void listsCarryMarkersOnTheirFirstBlock() {
        const RenderedMarkdown rendered = render("1. one\n2. two\n   - inner\n- [x] done\n- [ ] open\n");
        QCOMPARE(container(blockAt(rendered, 0), 0).value("marker").toString(), QStringLiteral("1."));
        QCOMPARE(container(blockAt(rendered, 1), 0).value("marker").toString(), QStringLiteral("2."));
        QCOMPARE(container(blockAt(rendered, 2), 0).value("marker").toString(), QString());
        QCOMPARE(container(blockAt(rendered, 2), 1).value("marker").toString(), QStringLiteral("◦"));
        QCOMPARE(container(blockAt(rendered, 3), 0).value("task").toInt(), 1);
        QCOMPARE(container(blockAt(rendered, 4), 0).value("task").toInt(), 0);
        QCOMPARE(blockAt(rendered, 1).value("gap").toInt(), 4);
    }

    void looseListsKeepParagraphSpacing() {
        const RenderedMarkdown rendered = render("- a\n\n- b\n\n  more\n");
        QCOMPARE(blockAt(rendered, 1).value("gap").toInt(), 16);
        QCOMPARE(blockAt(rendered, 2).value("gap").toInt(), 16);
        QCOMPARE(container(blockAt(rendered, 2), 0).value("marker").toString(), QString());
    }

    void nestedOrderedListsCountLikeGitHub() {
        const RenderedMarkdown rendered = render("1. a\n   1. b\n   2. c\n      1. d\n      2. e\n");
        QCOMPARE(container(blockAt(rendered, 2), 1).value("marker").toString(), QStringLiteral("ii."));
        QCOMPARE(container(blockAt(rendered, 4), 2).value("marker").toString(), QStringLiteral("b."));
    }

    void tablesKeepTheirAlignment() {
        const RenderedMarkdown rendered = render("| A | B | C |\n|:--|:-:|--:|\n| 1 | **2** |\n");
        const QVariantMap table = blockAt(rendered, 0);
        QCOMPARE(table.value("type").toString(), QStringLiteral("table"));
        QCOMPARE(table.value("aligns").toStringList(),
                 (QStringList{"left", "center", "right"}));
        QCOMPARE(table.value("header").toStringList(), (QStringList{"A", "B", "C"}));
        QCOMPARE(table.value("rows").toList().at(0).toStringList(),
                 (QStringList{"1", "<b>2</b>", ""}));
    }

    void footnotesAreNumberedInReferenceOrder() {
        const RenderedMarkdown rendered = render("Two[^b] and one[^a].\n\n[^a]: First.\n[^b]: Second.\n");
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("Two<sup><a href=\"#fn-1\">1</a></sup> and one<sup><a href=\"#fn-2\">2</a></sup>."));
        QCOMPARE(blockAt(rendered, 1).value("type").toString(), QStringLiteral("rule"));
        QVERIFY(blockAt(rendered, 1).value("thin").toBool());
        QCOMPARE(blockAt(rendered, 2).value("html").toString(),
                 QStringLiteral("Second. <a href=\"#fnref-1\">↩</a>"));
        QVERIFY(blockAt(rendered, 2).value("small").toBool());
        QCOMPARE(rendered.anchors.value("fn-1"), 2);
        QCOMPARE(rendered.anchors.value("fnref-1"), 0);
    }

    void imageParagraphsBecomeImages() {
        const RenderedMarkdown rendered = render(
            "![shot](img/a.png)\n\n[![ci](https://x.io/b.svg)](https://ci.io) ![c](/c.png)\n");
        const QVariantList shot = blockAt(rendered, 0).value("images").toList();
        QCOMPARE(shot.at(0).toMap().value("src").toString(), QStringLiteral("file:///repo/docs/img/a.png"));
        QCOMPARE(shot.at(0).toMap().value("alt").toString(), QStringLiteral("shot"));
        const QVariantList badges = blockAt(rendered, 1).value("images").toList();
        QCOMPARE(badges.size(), 2);
        QCOMPARE(badges.at(0).toMap().value("href").toString(), QStringLiteral("https://ci.io"));
        QCOMPARE(badges.at(1).toMap().value("src").toString(), QStringLiteral("file:///repo/c.png"));
    }

    void htmlImagesKeepSizeAndAlignment() {
        const RenderedMarkdown rendered = render(
            "<p align=\"center\">\n  <a href=\"https://site\"><img src='logo.png' width=\"200\" alt=logo></a>\n</p>\n");
        const QVariantMap block = blockAt(rendered, 0);
        QCOMPARE(block.value("type").toString(), QStringLiteral("images"));
        QCOMPARE(block.value("align").toString(), QStringLiteral("center"));
        const QVariantMap image = block.value("images").toList().at(0).toMap();
        QCOMPARE(image.value("src").toString(), QStringLiteral("file:///repo/docs/logo.png"));
        QCOMPARE(image.value("width").toInt(), 200);
        QCOMPARE(image.value("alt").toString(), QStringLiteral("logo"));
        QCOMPARE(image.value("href").toString(), QStringLiteral("https://site"));
    }

    void alignedDivsAlignTheMarkdownInside() {
        const RenderedMarkdown rendered = render("<div align=\"center\">\n\n# Title\n\nText\n\n</div>\n\nAfter\n");
        QCOMPARE(blockAt(rendered, 1).value("align").toString(), QStringLiteral("center"));
        QCOMPARE(blockAt(rendered, 2).value("align").toString(), QString());
    }

    void htmlLosesScriptsAndComments() {
        const RenderedMarkdown rendered = render(
            "<!-- hidden -->\n\n<p>Hi<script>alert(1)</script><style>p{}</style></p>\n");
        QCOMPARE(rendered.blocks.size(), 1);
        QCOMPARE(blockAt(rendered, 0).value("html").toString(), QStringLiteral("<p>Hi</p>"));
    }

    void detailsFoldTheirContent() {
        const RenderedMarkdown rendered = render(
            "<details>\n<summary>More <b>here</b></summary>\n\nText\n\n# Inside\n\n</details>\n\nAfter\n");
        QCOMPARE(rendered.blocks.size(), 4);
        const QVariantMap summary = blockAt(rendered, 0);
        QCOMPARE(summary.value("type").toString(), QStringLiteral("summary"));
        QCOMPARE(summary.value("html").toString(), QStringLiteral("More <b>here</b>"));
        QCOMPARE(summary.value("open").toBool(), false);
        QCOMPARE(summary.value("containers").toList().size(), 0);
        const QVariantMap details = container(blockAt(rendered, 1), 0);
        QCOMPARE(details.value("kind").toString(), QStringLiteral("details"));
        QCOMPARE(details.value("id"), summary.value("details"));
        QCOMPARE(details.value("open").toBool(), false);
        QCOMPARE(blockAt(rendered, 1).value("gap").toInt(), 0);
        QCOMPARE(container(blockAt(rendered, 2), 0).value("id"), summary.value("details"));
        QCOMPARE(blockAt(rendered, 3).value("containers").toList().size(), 0);
        QCOMPARE(rendered.anchors.value("inside"), 2);
    }

    void detailsWithoutMarkdownOrSummary() {
        const RenderedMarkdown rendered = render(
            "<details open><summary>All HTML</summary>\n<p>Body :tada:</p>\n</details>\n\n"
            "- <details>\n\n  Untitled\n\nOutside\n");
        QCOMPARE(rendered.blocks.size(), 5);
        QCOMPARE(blockAt(rendered, 0).value("open").toBool(), true);
        QCOMPARE(blockAt(rendered, 1).value("html").toString(), QStringLiteral("<p>Body 🎉</p>"));
        QCOMPARE(container(blockAt(rendered, 1), 0).value("open").toBool(), true);
        // No summary: GitHub calls it "Details". Left open in a list item, it
        // closes with the item.
        QCOMPARE(blockAt(rendered, 2).value("type").toString(), QStringLiteral("summary"));
        QCOMPARE(blockAt(rendered, 2).value("html").toString(), QStringLiteral("Details"));
        QCOMPARE(container(blockAt(rendered, 2), 0).value("marker").toString(), QStringLiteral("•"));
        QCOMPARE(container(blockAt(rendered, 3), 1).value("kind").toString(), QStringLiteral("details"));
        QCOMPARE(blockAt(rendered, 4).value("containers").toList().size(), 0);
    }

    void codeBlocksKeepTheirLanguage() {
        const RenderedMarkdown rendered = render("```bash title=x\necho <hi>\n```\n");
        QCOMPARE(blockAt(rendered, 0).value("code").toString(), QStringLiteral("echo <hi>"));
        QCOMPARE(blockAt(rendered, 0).value("language").toString(), QStringLiteral("bash"));
    }

    void codeIsHighlightedByLanguage() {
        const RenderedMarkdown rendered = render(
            "```python\ndef f():\n    return \"<hi>\"  # note\n```\n\n```\nplain\n```\n\n```nosuchlanguage\nx\n```\n");
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("<span class=\"kw\">def</span> f():\n"
                                "    <span class=\"kw\">return</span> <span class=\"str\">&quot;&lt;hi&gt;&quot;</span>"
                                "  <span class=\"com\"># note</span>"));
        // Without a language, or with one it does not know, code stays plain.
        QCOMPARE(blockAt(rendered, 1).value("html").toString(), QString());
        QCOMPARE(blockAt(rendered, 2).value("html").toString(), QString());
        // Fences name languages by extension, file name or GitHub's aliases.
        for (const char *language : {"sh", "console", "rs", "ts", "yml", "Dockerfile", "make", "c++"})
            QVERIFY2(!CodeHighlighter::html(QStringLiteral("x = 1"), QLatin1String(language)).isEmpty(), language);
    }

    void codeColoursFollowThePalette() {
        const QString light = CodeHighlighter::styleSheet(false);
        QVERIFY(light.contains(QStringLiteral(".kw { color: #cf222e; }")));
        QVERIFY(light.contains(QStringLiteral(".com { color: #59636e; }")));
        // An Omarchy theme's colours, as its Helix theme uses them.
        bool dark = false;
        const QString themed = Backend::themeColors(
            {{"background", "#101724"}, {"foreground", "#DEE2EC"}, {"magenta", "#B063BE"},
             {"green", "#5AC875"}, {"muted", "#76849B"}},
            false, &dark).value("syntax").toString();
        QVERIFY(themed.contains(QStringLiteral(".kw { color: #b063be; }")));
        QVERIFY(themed.contains(QStringLiteral(".str { color: #5ac875; }")));
        QVERIFY(themed.contains(QStringLiteral(".com { color: #76849b; font-style: italic; }")));
        QVERIFY(!themed.contains(QStringLiteral(".var ")));
    }

    void printedCodeIsHighlighted() {
        const QString html = PrintDocument::html(render("```c\nint x;\n```\n").blocks, {}, 600);
        QVERIFY(html.contains(QStringLiteral("<code><span class=\"ty\">int</span> x<span class=\"op\">;</span></code>")));
        QVERIFY(PrintDocument::styleSheet(QStringLiteral("Mono")).contains(QStringLiteral(".kw { color: #cf222e; }")));
    }

    void shortcodesBecomeEmoji() {
        const RenderedMarkdown rendered = render(
            "Ship it :tada: :+1::white_check_mark: :no_such_emoji: `:tada:` **:fire:**\n\n"
            "| :heart: |\n|---|\n\n"
            "<p align=\"center\">Made with :heart: <code>:heart:</code> <a title=\":x:\">:x:</a></p>\n\n"
            "```\n:tada:\n```\n");
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("Ship it 🎉 👍✅ :no_such_emoji: <code>:tada:</code> <b>🔥</b>"));
        QCOMPARE(blockAt(rendered, 1).value("header").toStringList(), QStringList{"❤️"});
        QCOMPARE(blockAt(rendered, 2).value("html").toString(),
                 QStringLiteral("<p align=\"center\">Made with ❤️ <code>:heart:</code> <a title=\":x:\">❌</a></p>"));
        QCOMPARE(blockAt(rendered, 3).value("code").toString(), QStringLiteral(":tada:"));
    }

    void mathIsTakenOutBeforeMarkdown() {
        // No pictures: each formula shows its TeX as code.
        const RenderedMarkdown rendered = render(
            "Area $\\pi r^2$, $a*b*c$ and $\\{x\\}$; $`a $ b`$ too. Costs $5 and $10, \\$x\\$ and `$code$`.\n\n"
            "$$\n\\frac{a}{b}\n$$\n\n"
            "```math\nE = mc^2\n```\n\n"
            "```sh\necho \"$A$B\"\n```\n\n"
            "# Size of $n$\n");
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("Area <code>\\pi r^2</code>, <code>a*b*c</code> and <code>\\{x\\}</code>; "
                                "<code>a $ b</code> too. Costs $5 and $10, $x$ and <code>$code$</code>."));
        QCOMPARE(blockAt(rendered, 1).value("type").toString(), QStringLiteral("code"));
        QCOMPARE(blockAt(rendered, 1).value("language").toString(), QStringLiteral("math"));
        QCOMPARE(blockAt(rendered, 1).value("code").toString(), QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(blockAt(rendered, 2).value("code").toString(), QStringLiteral("E = mc^2"));
        QCOMPARE(blockAt(rendered, 3).value("code").toString(), QStringLiteral("echo \"$A$B\""));
        QCOMPARE(blockAt(rendered, 4).value("anchor").toString(), QStringLiteral("size-of-n"));
        QCOMPARE(rendered.figures, 7);
    }

    void mathShowsAsPictures() {
        QStringList asked;
        const auto figure = [&asked](const QString &kind, const QString &source, bool display) {
            asked.append(QStringLiteral("%1:%2:%3").arg(kind, source).arg(display));
            return QVariantMap{{"src", "file:///cache/f.svg"}, {"width", 40.0}, {"height", 20.25}};
        };
        const RenderedMarkdown rendered = MarkdownRenderer::render(
            "> Inline $x$.\n>\n> $$\n> y\n> $$\n", baseUrl, rootUrl, figure);
        QCOMPARE(asked, (QStringList{"math:x:0", "math:y:1"}));
        QCOMPARE(blockAt(rendered, 0).value("html").toString(),
                 QStringLiteral("Inline <img class=\"math\" src=\"file:///cache/f.svg\" width=\"40.0\" "
                                "height=\"20.3\" style=\"vertical-align: middle;\">."));
        const QVariantMap display = blockAt(rendered, 1);
        QCOMPARE(display.value("type").toString(), QStringLiteral("images"));
        QCOMPARE(display.value("align").toString(), QStringLiteral("center"));
        QCOMPARE(display.value("images").toList().at(0).toMap().value("alt").toString(), QStringLiteral("y"));
        QCOMPARE(container(display, 0).value("kind").toString(), QStringLiteral("quote"));
    }

    void formulasAreDrawnByMathJax() {
        if (!Figures::canDraw(QStringLiteral("math")))
            QSKIP("node and mathjax are not installed");
        Figures figures;
        const QVariantMap colors{{"foreground", "#1f2328"}};
        QVERIFY(figures.picture("math", "x^2", false, colors, false).isEmpty());
        QVERIFY(figures.picture("math", "\\mathbb{R}", false, colors, false).isEmpty());
        figures.drawWantedNow(60000);

        const QVariantMap picture = figures.picture("math", "x^2", false, colors, false);
        QVERIFY(QFile::exists(QUrl(picture.value("src").toString()).toLocalFile()));
        // A little over an ex wide, and padded to sit on the text's middle.
        QVERIFY(picture.value("width").toReal() > 10 && picture.value("width").toReal() < 30);
        QVERIFY(picture.value("height").toReal() > 15);
        // Glyphs Arch's MathJax lacks: the formula stays as its TeX, and is
        // not asked for again.
        QVERIFY(figures.picture("math", "\\mathbb{R}", false, colors, false).isEmpty());
        figures.drawWanted();
        QVERIFY(!figures.drawing());
    }

    void mermaidBlocksBecomeDiagrams() {
        const QByteArray markdown = "```mermaid\ngraph LR\n  A --> B\n```\n";
        // No picture yet: the source shows as code.
        const RenderedMarkdown plain = render(markdown.constData());
        QCOMPARE(blockAt(plain, 0).value("type").toString(), QStringLiteral("code"));
        QCOMPARE(blockAt(plain, 0).value("language").toString(), QStringLiteral("mermaid"));
        QCOMPARE(plain.figures, 1);

        const auto figure = [](const QString &kind, const QString &source, bool) {
            return kind == QLatin1String("mermaid") && source == QLatin1String("graph LR\n  A --> B")
                ? QVariantMap{{"src", "file:///d.png"}, {"width", 120.0}, {"height", 60.0}}
                : QVariantMap();
        };
        const QVariantMap block = blockAt(MarkdownRenderer::render(markdown, baseUrl, rootUrl, figure), 0);
        QCOMPARE(block.value("type").toString(), QStringLiteral("images"));
        QCOMPARE(block.value("align").toString(), QStringLiteral("center"));
        QCOMPARE(block.value("images").toList().at(0).toMap().value("src").toString(), QStringLiteral("file:///d.png"));
    }

    void diagramsAreDrawnByMermaid() {
        if (!Figures::canDraw(QStringLiteral("mermaid")))
            QSKIP("mermaid-cli is not installed");
        Figures figures;
        const QVariantMap colors{{"background", "#ffffff"}, {"foreground", "#1f2328"}};
        QVERIFY(figures.picture("mermaid", "graph LR\n  A --> B", true, colors, false).isEmpty());
        QVERIFY(figures.picture("mermaid", "not a diagram", true, colors, false).isEmpty());
        figures.drawWantedNow(60000);

        // One that is not Mermaid spoils no other.
        const QVariantMap picture = figures.picture("mermaid", "graph LR\n  A --> B", true, colors, false);
        QVERIFY(QFile::exists(QUrl(picture.value("src").toString()).toLocalFile()));
        QVERIFY(picture.value("width").toReal() > picture.value("height").toReal());
        QVERIFY(figures.picture("mermaid", "not a diagram", true, colors, false).isEmpty());
        figures.drawWanted();
        QVERIFY(!figures.drawing());
    }

    void diagramsTakeTheThemesColours() {
        bool dark = false;
        const QVariantMap colors = Backend::themeColors(
            {{"background", "#101724"}, {"foreground", "#DEE2EC"}, {"blue", "#749FFF"},
             {"green", "#5AC875"}, {"yellow", "#EDD77B"}},
            false, &dark);
        const QJsonObject config = QJsonDocument::fromJson(
            Figures::style(QStringLiteral("mermaid"), colors, dark).toUtf8()).object();
        QCOMPARE(config.value("theme").toString(), QStringLiteral("base"));
        const QJsonObject variables = config.value("themeVariables").toObject();
        // Pie slices in the theme's own colours, not its greys.
        QCOMPARE(variables.value("pie1").toString(), QStringLiteral("#749fff"));
        QCOMPARE(variables.value("pie2").toString(), QStringLiteral("#5ac875"));
        QCOMPARE(variables.value("pie3").toString(), QStringLiteral("#edd77b"));
        QCOMPARE(variables.value("pieSectionTextColor").toString(), QStringLiteral("#101724"));

        // GitHub's palettes keep Mermaid's own themes, as github.com does.
        const QJsonObject github = QJsonDocument::fromJson(
            Figures::style(QStringLiteral("mermaid"), Backend::themeColors({}, true, &dark), true).toUtf8()).object();
        QCOMPARE(github.value("theme").toString(), QStringLiteral("dark"));
        QVERIFY(!github.value("themeVariables").toObject().contains("pie1"));
    }

    void missingProgramsAreNamed() {
        // Nothing to draw with on the PATH.
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        Figures figures;
        const QVariantMap colors{{"background", "#ffffff"}, {"foreground", "#1f2328"}};
        QCOMPARE(figures.picture("math", "x", false, colors, false), (QVariantMap{{"missing", true}}));
        QCOMPARE(figures.picture("mermaid", "graph LR", true, colors, false), (QVariantMap{{"missing", true}}));
        qputenv("PATH", path);

        const auto figure = [](const QString &, const QString &, bool) { return QVariantMap{{"missing", true}}; };
        const RenderedMarkdown rendered = MarkdownRenderer::render(
            "Inline $x$.\n\n$$\ny\n$$\n\n```mermaid\ngraph LR\n```\n", baseUrl, rootUrl, figure);
        // A formula in a line stays TeX, with no hint to crowd the text.
        QCOMPARE(blockAt(rendered, 0).value("html").toString(), QStringLiteral("Inline <code>x</code>."));
        QCOMPARE(blockAt(rendered, 1).value("code").toString(), QStringLiteral("y"));
        QVERIFY(blockAt(rendered, 1).value("hint").toString().contains(QStringLiteral("sudo pacman -S nodejs mathjax")));
        QCOMPARE(blockAt(rendered, 2).value("code").toString(), QStringLiteral("graph LR"));
        QVERIFY(blockAt(rendered, 2).value("hint").toString().contains(QStringLiteral("sudo pacman -S mermaid-cli")));

        // One drawing failed, or not drawn yet, says nothing.
        QVERIFY(!blockAt(render("```mermaid\ngraph LR\n```\n"), 0).contains(QStringLiteral("hint")));

        // Paper shows the source without the hint.
        const QString html = PrintDocument::html(rendered.blocks, {}, 600);
        QVERIFY(html.contains(QStringLiteral("graph LR")));
        QVERIFY(!html.contains(QStringLiteral("pacman")));
    }

    void printedFormulasFitThePrintedText() {
        const auto figure = [](const QString &, const QString &, bool) {
            return QVariantMap{{"src", "file:///f.svg"}, {"width", 32.0}, {"height", 16.0}};
        };
        const RenderedMarkdown rendered = MarkdownRenderer::render("Inline $x$.\n\n$$\ny\n$$\n", baseUrl, rootUrl, figure);
        const QString html = PrintDocument::html(rendered.blocks, {{"file:///f.svg", QSize(64, 32)}}, 600);
        // 11pt paper text is 11/12 of the screen's 16px.
        QVERIFY(html.contains(QStringLiteral("<img class=\"math\" src=\"file:///f.svg\" width=\"29.3\" height=\"14.7\"")));
        QVERIFY(html.contains(QStringLiteral("<img src=\"file:///f.svg\" width=\"29\" height=\"15\">")));
    }

    void gapsFollowGitHub() {
        const RenderedMarkdown rendered = render("Para\n# Head\nText\n\n---\nAfter\n");
        QCOMPARE(blockAt(rendered, 0).value("gap").toInt(), 0);
        QCOMPARE(blockAt(rendered, 1).value("gap").toInt(), 24);
        QCOMPARE(blockAt(rendered, 2).value("gap").toInt(), 16);
        QCOMPARE(blockAt(rendered, 3).value("gap").toInt(), 24);
        QCOMPARE(blockAt(rendered, 4).value("gap").toInt(), 24);
    }

    void urlsResolveLikeGitHub() {
        QCOMPARE(MarkdownRenderer::resolveUrl("../README.md#use", baseUrl, rootUrl),
                 QUrl("file:///repo/README.md#use"));
        QCOMPARE(MarkdownRenderer::resolveUrl("/src/main.cpp", baseUrl, rootUrl),
                 QUrl("file:///repo/src/main.cpp"));
        QCOMPARE(MarkdownRenderer::resolveUrl("/src/main.cpp", baseUrl, QUrl()),
                 QUrl("file:///src/main.cpp"));
        QCOMPARE(MarkdownRenderer::resolveUrl("//cdn.io/a.png", baseUrl, rootUrl),
                 QUrl("https://cdn.io/a.png"));
        QCOMPARE(MarkdownRenderer::resolveUrl("mailto:a@b.c", baseUrl, rootUrl), QUrl("mailto:a@b.c"));
    }

    void githubBlobImagesLoadRaw() {
        const RenderedMarkdown rendered = render("![a](https://github.com/o/r/blob/main/a.png)\n");
        QCOMPARE(blockAt(rendered, 0).value("images").toList().at(0).toMap().value("src").toString(),
                 QStringLiteral("https://github.com/o/r/raw/main/a.png"));
    }

    void printedAlertsKeepTheirColourAndQuotesGreyOut() {
        const QString html = PrintDocument::html(
            render("> [!CAUTION]\n> Hot.\n\n> Plain.\n").blocks, {}, 600);
        QVERIFY(html.contains(QStringLiteral("bgcolor=\"#d1242f\"")));
        QVERIFY(html.contains(QStringLiteral("font-weight: bold;\">Caution</p>")));
        QVERIFY(html.contains(QStringLiteral("color: #59636e;\">Plain.</p>")));
        QVERIFY(!html.contains(QStringLiteral("color: #59636e;\">Hot.</p>")));
    }

    void printedListsKeepTheirMarkers() {
        const QString html = PrintDocument::html(render("3. three\n   - [x] done\n").blocks, {}, 600);
        QVERIFY(html.contains(QStringLiteral(">3.</td>")));
        QVERIFY(html.contains(QStringLiteral(">☑</span></td>")));
    }

    void printedDetailsAreOpen() {
        const QString html = PrintDocument::html(
            render("<details>\n<summary>More</summary>\n\nHidden on screen\n\n</details>\n").blocks, {}, 600);
        QVERIFY(html.contains(QStringLiteral(">▼</span>&nbsp; More</p>")));
        QVERIFY(html.contains(QStringLiteral(">Hidden on screen</p>")));
    }

    void printedImagesFitThePage() {
        const RenderedMarkdown rendered = render(
            "![wide](wide.png)\n\n<img src=\"logo.svg\" width=\"100\">\n\n![gone](gone.png)\n");
        const QHash<QString, QSize> sizes{{"file:///repo/docs/wide.png", QSize(2000, 1000)},
                                          {"file:///repo/docs/logo.svg", QSize(50, 25)}};
        const QString html = PrintDocument::html(rendered.blocks, sizes, 600);
        QVERIFY(html.contains(QStringLiteral("src=\"file:///repo/docs/wide.png\" width=\"600\" height=\"300\"")));
        QVERIFY(html.contains(QStringLiteral("src=\"file:///repo/docs/logo.svg\" width=\"100\" height=\"50\"")));
        // An image that would not load prints its description.
        QVERIFY(html.contains(QStringLiteral(">gone </p>")));
    }

    void everyImageIsLoadedForPrinting() {
        const RenderedMarkdown rendered = render(
            "![a](a.png) and text ![q](https://x.io/b.svg?a=1&b=2)\n\n| ![c](c.png) |\n|---|\n\n![a](a.png)\n");
        QCOMPARE(PrintDocument::imageSources(rendered.blocks),
                 (QStringList{"file:///repo/docs/a.png", "https://x.io/b.svg?a=1&b=2",
                              "file:///repo/docs/c.png"}));
    }

    void themeColorsComeFromOmarchy() {
        bool dark = false;
        const QVariantMap colors = Backend::themeColors(
            {{"background", "#101724"}, {"foreground", "#DEE2EC"}, {"accent", "#3AA0EA"},
             {"lighter_background", "#1C2536"}, {"red", "#E8695A"}},
            false, &dark);
        QVERIFY(dark);
        QCOMPARE(colors.value("background").toString(), QStringLiteral("#101724"));
        QCOMPARE(colors.value("link").toString(), QStringLiteral("#3aa0ea"));
        QCOMPARE(colors.value("codeBackground").toString(), QStringLiteral("#1c2536"));
        QCOMPARE(colors.value("caution").toString(), QStringLiteral("#e8695a"));
        // Not in the theme: GitHub's dark palette.
        QCOMPARE(colors.value("tip").toString(), QStringLiteral("#3fb950"));
    }

    void withoutAThemeColorsAreGitHubs() {
        bool dark = true;
        const QVariantMap colors = Backend::themeColors({}, false, &dark);
        QVERIFY(!dark);
        QCOMPARE(colors.value("background").toString(), QStringLiteral("#ffffff"));
        QCOMPARE(colors.value("link").toString(), QStringLiteral("#0969da"));
    }

    void findIgnoresCaseAndTheAccentsTheQueryLeavesOut() {
        using Matches = QList<int>;
        const auto find = FindHighlighter::findMatches;
        QCOMPARE(find(QStringLiteral("Café, CAFE and cafe"), QStringLiteral("cafe")), Matches({0, 6, 15}));
        QCOMPARE(find(QStringLiteral("élève, Łódź, Straße"), QStringLiteral("lodz")), Matches({7}));
        // An accented letter in the query matches only itself.
        QCOMPARE(find(QStringLiteral("cafe café"), QStringLiteral("café")), Matches({5}));
        // Scripts whose marks make different letters keep them.
        QCOMPARE(find(QStringLiteral("й и"), QStringLiteral("и")), Matches({2}));
        QCOMPARE(find(QStringLiteral("aaaa"), QStringLiteral("aa")), Matches({0, 2}));
        QCOMPARE(find(QStringLiteral("text"), QString()), Matches());
    }

    void findMarksMatchesOverTheText() {
        QTextDocument document;
        document.setHtml(QStringLiteral("<p>One <b>one</b></p><p>none</p>"));
        QCOMPARE(FindHighlighter::mark(&document, QStringLiteral("one"), 4, false), QList<int>({0, 4, 9}));
        const QList<QTextLayout::FormatRange> first = document.begin().layout()->formats();
        QCOMPARE(first.size(), 2);
        QCOMPARE(first.at(0).format.background().color(), QColor("#ffe58a"));
        // The current match is stronger. Only the background is laid over
        // the text, so the bold stays.
        QCOMPARE(first.at(1).format.background().color(), QColor("#ffad42"));
        QCOMPARE(first.at(1).format.properties().size(), 1);
        QCOMPARE(document.begin().next().layout()->formats().size(), 1);

        // Text set again is marked again; an empty query clears the marks.
        document.setHtml(QStringLiteral("<p>one</p>"));
        QTRY_COMPARE(document.begin().layout()->formats().size(), 1);
        QCOMPARE(FindHighlighter::mark(&document, QString(), -1, false), QList<int>());
        QCOMPARE(document.begin().layout()->formats().size(), 0);
    }

    void shortcutsShowFromTheFooterAndF1() {
        Window view;
        QVERIFY(view.window);
        QObject *dialog = view.find<QObject>("shortcutsDialog");
        QVERIFY(dialog);
        QVERIFY(!dialog->property("visible").toBool());

        QTest::keyClick(view.window, Qt::Key_F1);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::keyClick(view.window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());

        view.click(view.find("shortcutsButton"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        // Every shortcut the window answers is listed.
        QCOMPARE(dialog->property("sections").toList().size(), 4);
        QVERIFY(view.find("openButton"));
    }

    void savesAPdfBesideTheFile() {
        QTemporaryDir dir;
        QFile markdown(dir.filePath("notes.md"));
        QVERIFY(markdown.open(QIODevice::WriteOnly));
        markdown.write("# Notes\n\nSome text.\n");
        markdown.close();

        Window view;
        QVERIFY(view.window);
        view.backend.open(QUrl::fromLocalFile(markdown.fileName()));
        const QUrl pdf = view.backend.pdfUrl();
        QCOMPARE(pdf, QUrl::fromLocalFile(dir.filePath("notes.pdf")));

        // The picker suggests it.
        QObject *dialog = view.find<QObject>("pdfDialog");
        QVERIFY(dialog);
        QTest::keyClick(view.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("selectedFile").toUrl(), pdf);
        QMetaObject::invokeMethod(dialog, "close");

        view.backend.savePdf(pdf);
        QFile saved(pdf.toLocalFile());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(saved.read(5) == "%PDF-");
        QCOMPARE(view.backend.status(), QStringLiteral("Saved notes.pdf"));
    }

    void reloadsFollowTheBlockBeingRead() {
        const auto follow = [](const char *before, int index, const char *after) {
            bool same = false;
            const int found = MarkdownRenderer::followBlock(render(before).blocks, index, render(after).blocks, &same);
            return QStringLiteral("%1%2").arg(found).arg(same ? "" : " changed");
        };
        // Blocks added or removed above.
        QCOMPARE(follow("A\n\nB\n\nC\n", 1, "X\n\nY\n\nA\n\nB\n\nC\n"), QStringLiteral("3"));
        QCOMPARE(follow("A\n\nB\n\nC\n", 2, "B\n\nC\n"), QStringLiteral("1"));
        // Lists number their items again; the item is the same.
        QCOMPARE(follow("1. a\n2. b\n3. c\n", 1, "1. z\n2. a\n3. b\n4. c\n"), QStringLiteral("2"));
        // The block itself was changed: the block now in its place.
        QCOMPARE(follow("A\n\nB\n\nC\n", 1, "A\n\nB, changed\n\nC\n"), QStringLiteral("1 changed"));
        QCOMPARE(follow("A\n\nB\n\nC\n\nD\n\nE\n", 2, "A\n\nnew\n\nB\n\nC, changed\n\nD\n\nE\n"),
                 QStringLiteral("3 changed"));
        // Removed: the block after it.
        QCOMPARE(follow("A\n\nB\n\nC\n", 1, "A\n\nC\n"), QStringLiteral("1 changed"));
        // Text found more than once: the copy nearest where it was.
        QCOMPARE(follow("A\n\nR\n\nB\n\nR\n\nC\n", 3, "A2\n\nX\n\nR\n\nB\n\nR\n\nC2\n"), QStringLiteral("4"));
        QCOMPARE(follow("A\n\nR\n\nB\n\nR\n\nC\n", 1, "A2\n\nX\n\nR\n\nB\n\nR\n\nC2\n"), QStringLiteral("2"));
        QCOMPARE(follow("A\n", 0, ""), QStringLiteral("-1 changed"));
    }

    void reloadsKeepThePlaceBeingRead() {
        QTemporaryDir dir;
        const QString path = dir.filePath("long.md");
        const auto write = [&path](const QByteArray &top, const QByteArray &edit = {}) {
            QByteArray text = top;
            for (int i = 0; i < 80; ++i) {
                text += (i == 40 && !edit.isEmpty() ? edit : "Paragraph " + QByteArray::number(i))
                    + " reads on for a while, long enough to wrap onto a second line when the window"
                      " is narrow, as prose does.\n\n";
            }
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text);
        };
        const auto place = [](QObject *window) {
            QVariant result;
            QMetaObject::invokeMethod(window, "placeOnScreen", Q_RETURN_ARG(QVariant, result));
            return result.toMap();
        };

        write("# Title\n\n");
        Window view;
        QVERIFY(view.window);
        QObject *flick = view.find<QObject>("flick");
        view.backend.open(QUrl::fromLocalFile(path));

        // Partway into a block halfway down.
        QTRY_VERIFY(flick->property("contentHeight").toReal() > 3000);
        QMetaObject::invokeMethod(view.window, "scrollTo", Q_ARG(QVariant, 2000.0));
        const QVariantMap before = place(view.window);
        QVERIFY(before.value("index").toInt() > 10);
        QVERIFY(before.value("offset").toReal() > 0);

        // Text added above: the same block, the same way into it.
        write("# Title\n\nNew.\n\nNewer.\n\n");
        view.backend.reload();
        QTRY_COMPARE(place(view.window).value("index").toInt(), before.value("index").toInt() + 2);
        QCOMPARE(place(view.window).value("offset").toReal(), before.value("offset").toReal());

        // Blocks above change height, as images loading do: the place holds.
        view.window->resize(560, 800);
        QTRY_COMPARE(view.window->width(), 560);
        QTRY_COMPARE(place(view.window).value("index").toInt(), before.value("index").toInt() + 2);
        QTRY_VERIFY(qAbs(place(view.window).value("offset").toReal() - before.value("offset").toReal()) < 1);

        // Until the reader scrolls, with the wheel or the keys.
        QVERIFY(!view.window->property("heldPlace").isNull());
        const QPointF middle(view.window->width() / 2.0, view.window->height() / 2.0);
        QWheelEvent wheel(middle, view.window->mapToGlobal(middle), QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view.window, &wheel);
        QTRY_VERIFY(view.window->property("heldPlace").isNull());
        QMetaObject::invokeMethod(view.window, "holdPlace", Q_ARG(QVariant, (QVariantMap{{"top", true}})));
        QTest::keyClick(view.window, Qt::Key_Down);
        QVERIFY(view.window->property("heldPlace").isNull());

        // A block changed where it is read: the block in its place.
        view.window->resize(1000, 800);
        QMetaObject::invokeMethod(view.window, "scrollTo", Q_ARG(QVariant, 2000.0));
        const int reading = place(view.window).value("index").toInt();
        QVERIFY(reading > 2 && reading < 80);
        write("# Title\n\nNew.\n\nNewer.\n\n", "");
        {
            // Paragraph i is block i + 3; change the one being read.
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QByteArray text = file.readAll();
            file.close();
            text.replace("Paragraph " + QByteArray::number(reading - 3) + " ", "Paragraph changed ");
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text);
        }
        view.backend.reload();
        QTRY_COMPARE(place(view.window).value("index").toInt(), reading);

        // At the very top, the page stays at the top.
        QMetaObject::invokeMethod(view.window, "scrollTo", Q_ARG(QVariant, 0.0));
        write("# Title\n\nNewest.\n\nNew.\n\nNewer.\n\n");
        view.backend.reload();
        QTest::qWait(100);
        QCOMPARE(flick->property("contentY").toReal(), 0.0);

        // Another file starts at the top.
        QMetaObject::invokeMethod(view.window, "scrollTo", Q_ARG(QVariant, 2000.0));
        const QString other = dir.filePath("other.md");
        QVERIFY(QFile::copy(path, other));
        view.backend.open(QUrl::fromLocalFile(other));
        QTRY_COMPARE(flick->property("contentY").toReal(), 0.0);
    }

    void headingsKeepTheirPlainText() {
        const QVariantMap heading = blockAt(render("# Size of `n` and $x$ :tada:\n"), 0);
        QCOMPARE(heading.value("text").toString(), QStringLiteral("Size of n and x 🎉"));
    }

    void theOutlineListsAndGoesToHeadings() {
        QTemporaryDir dir;
        const QString path = dir.filePath("guide.md");
        const auto write = [&path](const QByteArray &extra) {
            QByteArray text = "# Guide\n\n";
            const auto prose = [&text](int count) {
                for (int i = 0; i < count; ++i)
                    text += "A paragraph long enough to take up some room on the page, and then a little more.\n\n";
            };
            prose(20);
            text += "## Setup\n\n";
            prose(20);
            text += "<details>\n<summary>More</summary>\n\n### Hidden\n\nTucked away.\n\n</details>\n\n## Use\n\n";
            prose(30);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text + extra);
        };
        const auto place = [](QObject *window) {
            QVariant result;
            QMetaObject::invokeMethod(window, "placeOnScreen", Q_RETURN_ARG(QVariant, result));
            return result.toMap().value("index").toInt();
        };

        write({});
        Window view;
        QVERIFY(view.window);
        view.backend.open(QUrl::fromLocalFile(path));
        QQuickItem *outline = view.find("outline");
        QVERIFY(outline);
        QVERIFY(!outline->isVisible());

        // Ctrl+Shift+O shows it, and that is remembered.
        QTest::keyClick(view.window, Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(outline->isVisible());
        QVERIFY(QSettings().value("outline/open").toBool());
        const QVariantList headings = outline->property("headings").toList();
        QCOMPARE(headings.size(), 4);
        QCOMPARE(headings.at(2).toMap().value("text").toString(), QStringLiteral("Hidden"));
        QCOMPARE(outline->property("topLevel").toInt(), 1);
        // Beside the page in a wide window.
        QVERIFY(!outline->property("floating").toBool());

        // A click goes to its heading, which is then the current one.
        view.click(view.findShown("outlineEntry3"));
        const int use = headings.at(3).toMap().value("index").toInt();
        QTRY_COMPARE(place(view.window), use);
        QTRY_COMPARE(outline->property("current").toInt(), 3);

        // A heading in a closed <details> opens it.
        view.click(view.findShown("outlineEntry2"));
        const int hidden = headings.at(2).toMap().value("index").toInt();
        QTRY_COMPARE(place(view.window), hidden);
        QTRY_COMPARE(outline->property("current").toInt(), 2);

        // Over the page in a narrow window.
        view.window->resize(560, 800);
        QTRY_VERIFY(outline->property("floating").toBool());
        // Esc closes it there, as a popup.
        QTest::keyClick(view.window, Qt::Key_Escape);
        QTRY_VERIFY(!outline->isVisible());
        view.backend.setOutlineOpen(true);
        view.window->resize(1000, 800);
        QTRY_VERIFY(!outline->property("floating").toBool());

        // A reload brings new headings in, and keeps the place being read.
        QMetaObject::invokeMethod(view.window, "scrollTo", Q_ARG(QVariant, 1500.0));
        const int reading = place(view.window);
        write("## Later\n\nAdded.\n");
        view.backend.reload();
        QTRY_COMPARE(outline->property("headings").toList().size(), 5);
        QTRY_COMPARE(place(view.window), reading);

        // Without headings there is no outline, and nothing to toggle.
        const QString plain = dir.filePath("plain.md");
        QFile file(plain);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Just a paragraph.\n");
        file.close();
        view.backend.open(QUrl::fromLocalFile(plain));
        QTRY_VERIFY(!outline->isVisible());
        QTest::keyClick(view.window, Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(view.backend.outlineOpen());
        QVERIFY(!view.find("outlineButton")->isEnabled());

        // Ctrl+Shift+O hides it again.
        view.backend.open(QUrl::fromLocalFile(path));
        QTRY_VERIFY(outline->isVisible());
        QTest::keyClick(view.window, Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!outline->isVisible());
        QVERIFY(!QSettings().value("outline/open").toBool());
    }

    void markdownFilesAreRecognised() {
        QVERIFY(Backend::isMarkdown("notes/README.MD"));
        QVERIFY(Backend::isMarkdown("a.markdown"));
        QVERIFY(!Backend::isMarkdown("a.txt"));
    }

    void versionAnswersWithoutAWindow() {
        QCOMPARE(Cli::handleArguments(QStringList{"omaread", "--version"}, "1.2.3"), std::optional<int>(0));
        QCOMPARE(Cli::handleArguments(QStringList{"omaread", "README.md"}, "1.2.3"), std::nullopt);
    }
};

QTEST_MAIN(OmareadTest)
#include "tst_omaread.moc"
