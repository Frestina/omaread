#include "codehighlighter.h"

#include <QColor>
#include <QMap>
#include <QStringList>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

namespace {

using Style = KSyntaxHighlighting::Theme::TextStyle;

// The kinds of token GitHub colours apart, as the class each one's span
// carries.
QString styleClass(Style style) {
    switch (style) {
    case Style::Keyword:
    case Style::ControlFlow:
    case Style::Import:
        return QStringLiteral("kw");
    case Style::Preprocessor:
        return QStringLiteral("pp");
    case Style::Operator:
        return QStringLiteral("op");
    case Style::Function:
        return QStringLiteral("fn");
    case Style::Variable:
        return QStringLiteral("var");
    case Style::BuiltIn:
        return QStringLiteral("bi");
    case Style::DataType:
    case Style::Extension:
        return QStringLiteral("ty");
    case Style::Attribute:
        return QStringLiteral("at");
    case Style::DecVal:
    case Style::BaseN:
    case Style::Float:
    case Style::Constant:
        return QStringLiteral("num");
    case Style::Char:
    case Style::String:
    case Style::VerbatimString:
        return QStringLiteral("str");
    case Style::SpecialChar:
        return QStringLiteral("esc");
    case Style::SpecialString:
        return QStringLiteral("re");
    case Style::Comment:
    case Style::Documentation:
    case Style::Annotation:
    case Style::CommentVar:
    case Style::RegionMarker:
    case Style::Information:
    case Style::Warning:
    case Style::Alert:
        return QStringLiteral("com");
    case Style::Error:
        return QStringLiteral("err");
    default:
        return {};
    }
}

const KSyntaxHighlighting::Repository &repository() {
    // Reading the definitions takes a moment, so it waits for the first code
    // block that names a language.
    static const KSyntaxHighlighting::Repository repository;
    return repository;
}

// The language a fence names, as GitHub's info strings do: by name ("bash",
// "C++"), by file extension ("py", "rs") or by file name ("Dockerfile").
KSyntaxHighlighting::Definition definitionFor(const QString &language) {
    static const QHash<QString, QString> aliases{
        {QStringLiteral("shell"), QStringLiteral("Bash")},
        {QStringLiteral("console"), QStringLiteral("Bash")},
        {QStringLiteral("shell-session"), QStringLiteral("Bash")},
        {QStringLiteral("golang"), QStringLiteral("Go")},
        {QStringLiteral("make"), QStringLiteral("Makefile")},
        {QStringLiteral("objc"), QStringLiteral("Objective-C")},
        {QStringLiteral("viml"), QStringLiteral("Vim Script")},
        {QStringLiteral("vim"), QStringLiteral("Vim Script")}};
    const QString lower = language.toLower();
    const QString name = aliases.value(lower, language);
    KSyntaxHighlighting::Definition definition = repository().definitionForName(name);
    if (!definition.isValid())
        definition = repository().definitionForFileName(QStringLiteral("file.") + lower);
    if (!definition.isValid())
        definition = repository().definitionForFileName(language);
    return definition;
}

class SpanWriter : public KSyntaxHighlighting::AbstractHighlighter {
public:
    explicit SpanWriter(const KSyntaxHighlighting::Definition &definition) {
        setDefinition(definition);
    }

    QString write(const QString &code) {
        KSyntaxHighlighting::State state;
        const QStringList lines = code.split(QLatin1Char('\n'));
        for (qsizetype index = 0; index < lines.size(); ++index) {
            if (index > 0)
                m_out += QLatin1Char('\n');
            m_line = lines.at(index);
            m_written = 0;
            m_lastStyle.clear();
            state = highlightLine(m_line, state);
            m_out += m_line.mid(m_written).toHtmlEscaped();
        }
        return m_out;
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override {
        if (offset > m_written) {
            m_out += m_line.mid(m_written, offset - m_written).toHtmlEscaped();
            m_lastStyle.clear();
        }
        const QString text = m_line.mid(offset, length).toHtmlEscaped();
        const QString style = styleClass(format.textStyle());
        if (style.isEmpty()) {
            m_out += text;
        } else if (style == m_lastStyle) {
            // A token of the same kind as the one before joins its span.
            m_out.insert(m_out.size() - int(qstrlen("</span>")), text);
        } else {
            m_out += QStringLiteral("<span class=\"%1\">%2</span>").arg(style, text);
        }
        m_lastStyle = style;
        m_written = offset + length;
    }

private:
    QString m_out;
    QString m_line;
    int m_written = 0;
    QString m_lastStyle;
};

}

QString CodeHighlighter::html(const QString &code, const QString &language) {
    if (language.isEmpty())
        return {};
    const KSyntaxHighlighting::Definition definition = definitionFor(language);
    if (!definition.isValid())
        return {};
    return SpanWriter(definition).write(code);
}

QString CodeHighlighter::styleSheet(bool darkMode, const QHash<QString, QString> &theme) {
    // GitHub's own colours for each kind of token.
    QMap<QString, QString> rules = darkMode
        ? QMap<QString, QString>{{"kw", "#ff7b72"}, {"pp", "#ff7b72"}, {"op", "#ff7b72"},
                                  {"fn", "#d2a8ff"}, {"var", "#ffa657"}, {"bi", "#79c0ff"},
                                  {"ty", "#ffa657"}, {"at", "#79c0ff"}, {"num", "#79c0ff"},
                                  {"str", "#a5d6ff"}, {"esc", "#7ee787"}, {"re", "#7ee787"},
                                  {"com", "#9198a1"}, {"err", "#ffa198"}}
        : QMap<QString, QString>{{"kw", "#cf222e"}, {"pp", "#cf222e"}, {"op", "#cf222e"},
                                  {"fn", "#6639ba"}, {"var", "#953800"}, {"bi", "#0550ae"},
                                  {"ty", "#953800"}, {"at", "#0550ae"}, {"num", "#0550ae"},
                                  {"str", "#0a3069"}, {"esc", "#116329"}, {"re", "#116329"},
                                  {"com", "#59636e"}, {"err", "#82071e"}};
    for (auto it = rules.begin(); it != rules.end(); ++it)
        it.value() = QStringLiteral("color: %1;").arg(it.value());

    // An Omarchy theme colours them as its Helix theme does.
    const auto use = [&rules, &theme](const char *style, const char *themeKey, const char *extra = "") {
        const QColor color(theme.value(QLatin1String(themeKey)));
        if (color.isValid())
            rules[QLatin1String(style)] = QStringLiteral("color: %1;%2").arg(color.name(), QLatin1String(extra));
    };
    use("kw", "magenta");
    use("pp", "magenta");
    use("op", "cyan");
    use("fn", "blue");
    // Variables are plain text there.
    if (QColor(theme.value(QStringLiteral("foreground"))).isValid())
        rules.remove(QStringLiteral("var"));
    use("bi", "red");
    use("ty", "yellow");
    use("at", "yellow");
    use("num", "yellow");
    use("str", "green");
    use("esc", "magenta");
    use("re", "magenta");
    use("com", "muted", " font-style: italic;");
    use("err", "red");

    QString sheet;
    for (auto it = rules.cbegin(); it != rules.cend(); ++it)
        sheet += QStringLiteral(".%1 { %2 } ").arg(it.key(), it.value());
    return sheet.trimmed();
}
