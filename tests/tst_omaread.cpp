#include <QtTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "cli.h"
#include "codehighlighter.h"
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
}

class OmareadTest : public QObject {
    Q_OBJECT

private slots:
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
