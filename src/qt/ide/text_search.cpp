#include "text_search.hpp"
#include "language_service.hpp"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace picoide {

QRegularExpression searchExpression(const QString& query, bool regex, bool matchCase) {
    auto options = QRegularExpression::PatternOptions(QRegularExpression::MultilineOption |
                                                       QRegularExpression::UseUnicodePropertiesOption);
    if (!matchCase) options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(regex ? query : QRegularExpression::escape(query), options);
}

bool needsPattern(TextOperation operation) {
    return operation != TextOperation::AtStart && operation != TextOperation::AtEnd;
}

QString replacementFor(const QRegularExpressionMatch& match, const QString& text, bool regex) {
    if (!regex) return text;
    // Expand against the original match, including captures inside assertions.
    static const QRegularExpression token(R"(\$\$|\$(\d{1,2})|\$\{([A-Za-z_]\w*)\}|\\([1-9]\d?)|\\([nrt\\]))");
    QString result;
    int end = 0;
    auto parts = token.globalMatch(text);
    while (parts.hasNext()) {
        const auto part = parts.next();
        result += text.mid(end, part.capturedStart() - end);
        if (part.captured() == "$$") {
            result += '$';
        } else if (!part.captured(4).isEmpty()) {
            const auto character = part.captured(4).at(0);
            if (character == 'n') result += '\n';
            else if (character == 'r') result += '\r';
            else if (character == 't') result += '\t';
            else result += character;
        } else if (!part.captured(2).isEmpty()) {
            const auto name = part.captured(2);
            result += match.regularExpression().namedCaptureGroups().contains(name) ? match.captured(name) : part.captured();
        } else {
            const int index = (part.captured(1).isEmpty() ? part.captured(3) : part.captured(1)).toInt();
            result += index <= match.regularExpression().captureCount() ? match.captured(index) : part.captured();
        }
        end = part.capturedEnd();
    }
    result += text.mid(end);
    return result;
}

QString replaceMatch(const QRegularExpressionMatch& match, const QString& text, bool regex, TextOperation operation) {
    const auto value = replacementFor(match, text, regex);
    if (operation == TextOperation::BeforeMatch) return value + match.captured();
    if (operation == TextOperation::AfterMatch) return match.captured() + value;
    return value;
}

bool transformText(TextEdit& edit, const QRegularExpression& expression, const QString& replacement,
                   bool regex, TextOperation operation) {
    // Bound expansion before appending. A zero-width regex can match every position.
    constexpr int limit = 2 * 1024 * 1024;
    edit.after.clear();
    edit.count = 0;
    if (!needsPattern(operation)) {
        if (edit.before.size() + replacement.size() > limit) return false;
        edit.after = operation == TextOperation::AtStart ? replacement + edit.before : edit.before + replacement;
        edit.count = edit.after != edit.before ? 1 : 0;
        return edit.after.toUtf8().size() <= limit;
    }
    int offset = 0;
    auto matches = expression.globalMatch(edit.before);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto value = replaceMatch(match, replacement, regex, operation);
        const int unchanged = match.capturedStart() - offset;
        if (edit.after.size() + unchanged + value.size() > limit) return false;
        edit.after += edit.before.mid(offset, unchanged);
        edit.after += value;
        offset = match.capturedEnd();
        ++edit.count;
    }
    if (edit.after.size() + edit.before.size() - offset > limit) return false;
    edit.after += edit.before.mid(offset);
    return edit.after.toUtf8().size() <= limit;
}

namespace {
const QRegularExpression cToken(
    R"tok(R"([^()\\\s]{0,16})\([\s\S]*?\)\1"|//(?:\\\n|[^\n])*|/\*[\s\S]*?(?:\*/|\z)|"(?:\\[\s\S]|[^"\\\n])*"|'(?:\\[\s\S]|[^'\\\n])*')tok");
const QRegularExpression hashToken(R"tok(#[^\n]*|"(?:\\[\s\S]|[^"\\\n])*"|'(?:\\[\s\S]|[^'\\\n])*')tok");

using TextToken = std::function<void(const QString&, int)>;

void visitComments(const QString& text, const QRegularExpression& lexer, const TextToken& visit) {
    auto tokens = lexer.globalMatch(text);
    while (tokens.hasNext()) {
        const auto token = tokens.next();
        const auto part = token.captured();
        if (part.startsWith('/') || part.startsWith('#')) visit(part, token.capturedStart());
    }
}

bool isMarkdown(const QString& suffix) {
    return suffix == "md" || suffix == "markdown";
}

void visitDoxygenCommands(const QString& text, int offset, const TextToken& visit) {
    auto matches = doxygenCommand().globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        visit(match.captured(), offset + match.capturedStart());
    }
}
}

bool isDoxygenComment(const QString& text) {
    return text.startsWith("/**") || text.startsWith("/*!") ||
           text.startsWith("///") || text.startsWith("//!");
}

const QRegularExpression& doxygenCommand() {
    // Discover command tokens, including custom aliases. Doxygen interprets
    // the command grammar, resolves arguments, and expands aliases.
    static const QRegularExpression command(
        R"cmd((?:(?<![\\@])\\|(?<![\w\\@])@)(?:f[()\[\]{}$]|[A-Za-z_][A-Za-z0-9_]*|~[A-Za-z]*|---?|::|[\\@${}#<>%".=|&?!]))cmd");
    return command;
}

void findDoxygen(const QString& path, const QString& text,
                 const std::function<void(TextRange, const QString&)>& found) {
    const auto collect = [&](const QString& part, int offset) {
        visitDoxygenCommands(part, offset, [&](const QString& command, int start) {
            found({start, static_cast<int>(command.size())}, command);
        });
    };
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (isMarkdown(suffix) || suffix == "dox") {
        collect(text, 0);
    } else if (languageForFile(path).highlight == Highlight::C) {
        visitComments(text, cToken, [&](const QString& part, int start) {
            if (isDoxygenComment(part)) collect(part, start);
        });
    }
}

void findTodo(const QString& path, const QString& text,
              const std::function<void(TextRange, const QString&)>& found) {
    static const QRegularExpression marker(R"(\b(TODO|FIXME|HACK|XXX|BUG)\b)",
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression checklist(R"(^[^\S\n]*[-*+][^\S\n]+\[ \][^\S\n]+)", QRegularExpression::MultilineOption);
    static const QRegularExpression xrefKey(R"(\G[ \t]+(todo|test|bug|deprecated)(?!\w))",
                                            QRegularExpression::CaseInsensitiveOption);
    const auto isMd = isMarkdown(QFileInfo(path).suffix().toLower());
    std::vector<int> markdownTags;
    const auto report = [&](TextRange range, const QString& kind) {
        if (isMd) markdownTags.push_back(range.start);
        found(range, kind);
    };
    const auto emitMarkers = [&](const QString& part, int start, bool documentation) {
        QSet<int> doxygenTasks;
        if (documentation) {
            visitDoxygenCommands(part, 0, [&](const QString& command, int pos) {
                auto kind = command.mid(1).toUpper();
                if (kind == "XREFITEM") {
                    const int arguments = pos + command.size();
                    const auto xref = xrefKey.match(part, arguments);
                    if (!xref.hasMatch()) return;
                    kind = xref.captured(1).toUpper();
                    doxygenTasks.insert(xref.capturedStart(1));
                } else if (kind != "TODO" && kind != "TEST" && kind != "BUG" && kind != "DEPRECATED") {
                    return;
                }
                doxygenTasks.insert(pos + 1);
                report({start + pos, static_cast<int>(command.size())}, kind);
            });
        }
        auto markers = marker.globalMatch(part);
        while (markers.hasNext()) {
            const auto match = markers.next();
            if (doxygenTasks.contains(match.capturedStart())) continue;
            report({start + static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())},
                   match.captured().toUpper());
        }
    };

    const QFileInfo info(path);
    const auto suffix = info.suffix().toLower();
    const bool cStyle = languageForFile(path).highlight == Highlight::C || suffix == "js" || suffix == "css";
    const bool hashStyle = suffix == "cmake" || suffix == "py" || suffix == "sh" ||
                           info.fileName() == "CMakeLists.txt" || info.fileName() == "Makefile";
    if (cStyle || hashStyle) {
        visitComments(text, cStyle ? cToken : hashToken, [&](const QString& part, int start) {
            emitMarkers(part, start, cStyle && isDoxygenComment(part));
        });
        return;
    }

    emitMarkers(text, 0, isMd || suffix == "dox");
    if (isMd) {
        std::sort(markdownTags.begin(), markdownTags.end());
        auto matches = checklist.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            // A checklist line already labelled TODO, FIXME, etc. appears once.
            const int start = match.capturedEnd();
            const int end = text.indexOf('\n', start);
            const auto tagged = std::lower_bound(markdownTags.begin(), markdownTags.end(), start);
            if (tagged == markdownTags.end() || (end >= 0 && *tagged >= end))
                found({start, 0}, "TODO");
        }
    }
}

ProjectTextScan::ProjectTextScan(QObject* parent) : QObject(parent) {
    connect(&timer_, &QTimer::timeout, this, &ProjectTextScan::advance);
}

ProjectTextScan::~ProjectTextScan() = default;

void ProjectTextScan::start(const QString& root) {
    cancel();
    if (root.isEmpty()) return;
    addDirectory(root);
    timer_.start(0);
}

void ProjectTextScan::cancel() {
    timer_.stop();
    directories_.clear();
}

void ProjectTextScan::addDirectory(const QString& path) {
    directories_.emplace_back(new QDirIterator(path, QDir::Files | QDir::Dirs | QDir::Hidden |
                                                     QDir::NoDotAndDotDot | QDir::NoSymLinks));
}

void ProjectTextScan::advance() {
    QElapsedTimer slice;
    slice.start();
    while (!directories_.empty() && slice.elapsed() < 8) {
        auto& directory = *directories_.back();
        if (!directory.hasNext()) {
            directories_.pop_back();
            continue;
        }
        const auto path = directory.next();
        const auto info = directory.fileInfo();
        if (info.isDir()) {
            const auto name = info.fileName();
            if (name == ".git" || name == ".picodbg" || QFileInfo(QDir(path).filePath("CMakeCache.txt")).isFile()) continue;
            addDirectory(path);
        } else if (info.isFile() && info.fileName() != ".git" && file) {
            file(path);
        }
    }
    if (directories_.empty()) {
        timer_.stop();
        if (finished) finished();
    }
}

}
