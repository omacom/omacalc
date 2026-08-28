#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QRect>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>
#include <cmath>

namespace {
const auto windowGeometrySetting = QStringLiteral("window/geometry");

const QString plusSign = QStringLiteral("+");
const QString minusSign = QStringLiteral("−");
const QString multiplySign = QStringLiteral("×");
const QString divideSign = QStringLiteral("÷");
const QString openParen = QStringLiteral("(");
const QString closeParen = QStringLiteral(")");

bool isOperator(const QString &token) {
    return token == plusSign || token == minusSign
        || token == multiplySign || token == divideSign;
}

bool isParen(const QString &token) {
    return token == openParen || token == closeParen;
}

int matchingOpenParen(const QStringList &tokens, int closeIndex) {
    int depth = 0;
    for (int i = closeIndex; i >= 0; --i) {
        if (tokens.at(i) == closeParen) {
            ++depth;
        } else if (tokens.at(i) == openParen && --depth == 0) {
            return i;
        }
    }
    return -1;
}

// Return the first token in the innermost group that is still open. This lets
// calculator operations work on the local running total without trying to
// evaluate the incomplete outer expression.
int openGroupExpressionStart(const QStringList &tokens) {
    int depth = 0;
    for (int i = tokens.size() - 1; i >= 0; --i) {
        if (tokens.at(i) == closeParen) {
            ++depth;
        } else if (tokens.at(i) == openParen) {
            if (depth == 0)
                return i + 1;
            --depth;
        }
    }
    return 0;
}

// Recursive-descent parser over the flat token list, giving × and ÷ their
// usual precedence over + and − and letting parentheses override both.
double parseExpression(const QStringList &tokens, int &pos, bool &ok);

double parseFactor(const QStringList &tokens, int &pos, bool &ok) {
    if (pos >= tokens.size() || isOperator(tokens.at(pos)) || tokens.at(pos) == closeParen) {
        ok = false;
        return 0;
    }
    if (tokens.at(pos) == openParen) {
        ++pos;
        const double value = parseExpression(tokens, pos, ok);
        if (!ok)
            return 0;
        if (pos >= tokens.size() || tokens.at(pos) != closeParen) {
            ok = false;
            return 0;
        }
        ++pos;
        return value;
    }

    bool numberOk = false;
    const double value = QLocale::c().toDouble(tokens.at(pos), &numberOk);
    if (!numberOk) {
        ok = false;
        return 0;
    }
    ++pos;
    return value;
}

double parseTerm(const QStringList &tokens, int &pos, bool &ok) {
    double value = parseFactor(tokens, pos, ok);
    if (!ok)
        return 0;

    while (pos < tokens.size()
           && (tokens.at(pos) == multiplySign || tokens.at(pos) == divideSign)) {
        const QString op = tokens.at(pos++);
        const double operand = parseFactor(tokens, pos, ok);
        if (!ok)
            return 0;
        value = (op == multiplySign) ? value * operand : value / operand;
    }
    return value;
}

double parseExpression(const QStringList &tokens, int &pos, bool &ok) {
    double value = parseTerm(tokens, pos, ok);
    if (!ok)
        return 0;

    while (pos < tokens.size()
           && (tokens.at(pos) == plusSign || tokens.at(pos) == minusSign)) {
        const QString op = tokens.at(pos++);
        const double operand = parseTerm(tokens, pos, ok);
        if (!ok)
            return 0;
        value = (op == plusSign) ? value + operand : value - operand;
    }
    return value;
}

// Digits are entered raw, so "5." and "-" can linger while typing. Seal them
// into plain numbers before they join the expression.
QString sealNumber(const QString &entry) {
    QString sealed = entry;
    if (sealed.endsWith(QLatin1Char('.')))
        sealed.chop(1);
    if (sealed.isEmpty() || sealed == QStringLiteral("-"))
        return QStringLiteral("0");
    return sealed;
}
}

Backend::Backend(QObject *parent) : QObject(parent) {
    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

QString Backend::expression() const {
    if (m_errored || m_justEvaluated)
        return m_evaluatedExpression;
    return prettyExpression(m_tokens);
}

QString Backend::display() const {
    if (m_errored)
        return QStringLiteral("Error");
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_justEvaluated)
        return m_result;
    return formatNumber(QLocale::c().toDouble(currentValue()));
}

// Operand tokens carry full round-trip precision when they come from a chained
// result; present every number at display precision instead.
QString Backend::prettyExpression(const QStringList &tokens) {
    QString pretty;
    for (const QString &token : tokens) {
        const QString piece = (isOperator(token) || isParen(token))
            ? token : formatNumber(QLocale::c().toDouble(token));
        // No space after an opening paren or before a closing one, so groups
        // read as "(2 + 3)" rather than "( 2 + 3 )".
        if (!pretty.isEmpty() && token != closeParen && !pretty.endsWith(openParen))
            pretty += QLatin1Char(' ');
        pretty += piece;
    }
    return pretty;
}

// The number the calculator is "at" right now: the entry being typed, the
// total of a group just closed, the operand the last operator was applied
// to, or the fresh-start zero.
QString Backend::currentValue() const {
    if (!m_entry.isEmpty())
        return sealNumber(m_entry);

    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        const QString &token = m_tokens.at(i);
        if (isOperator(token) || token == openParen)
            continue;
        if (token == closeParen) {
            const int start = matchingOpenParen(m_tokens, i);
            if (start >= 0) {
                bool ok = false;
                const double value = evaluateTokens(m_tokens.mid(start, i - start + 1), &ok);
                if (ok)
                    return QString::number(value, 'g', 17);
            }
            continue;
        }
        if (!isParen(token))
            return token;
    }
    return QStringLiteral("0");
}

void Backend::pressKey(const QString &key) {
    if (key.size() == 1 && key.at(0).isDigit()) {
        pressDigit(key);
    } else if (key == QStringLiteral(".")) {
        pressDecimal();
    } else if (key == QStringLiteral("+")) {
        pressOperator(plusSign);
    } else if (key == QStringLiteral("-") || key == minusSign) {
        pressOperator(minusSign);
    } else if (key == QStringLiteral("*") || key == multiplySign) {
        pressOperator(multiplySign);
    } else if (key == QStringLiteral("/") || key == divideSign) {
        pressOperator(divideSign);
    } else if (key == QStringLiteral("=")) {
        pressEquals();
    } else if (key == QStringLiteral("%")) {
        pressPercent();
    } else if (key == QStringLiteral("sign")) {
        pressToggleSign();
    } else if (key == QStringLiteral("backspace")) {
        pressBackspace();
    } else if (key == QStringLiteral("clear")) {
        pressClear();
    } else if (key == openParen) {
        pressOpenParen();
    } else if (key == closeParen) {
        pressCloseParen();
    } else {
        return;
    }

    emit calculationChanged();
}

void Backend::pressDigit(const QString &digit) {
    if (m_errored)
        pressClear();
    if (m_justEvaluated) {
        // A digit after equals starts a new calculation rather than
        // appending to the result.
        pressClear();
    }

    if (m_entry == QStringLiteral("0")) {
        m_entry = digit;
        return;
    }
    if (m_entry == QStringLiteral("-0")) {
        m_entry = QStringLiteral("-") + digit;
        return;
    }

    // Fifteen significant digits is what the display format preserves; letting
    // the entry grow beyond that would silently round what was typed. The
    // zero in a leading "0." is not significant, so it does not count.
    int digits = 0;
    for (const QChar character : m_entry) {
        if (character.isDigit())
            ++digits;
    }
    if (m_entry.startsWith(QStringLiteral("0."))
            || m_entry.startsWith(QStringLiteral("-0.")))
        --digits;
    if (digits < 15)
        m_entry += digit;
}

void Backend::pressDecimal() {
    if (m_errored)
        pressClear();
    if (m_justEvaluated)
        pressClear();

    if (m_entry.isEmpty())
        m_entry = QStringLiteral("0.");
    else if (!m_entry.contains(QLatin1Char('.')))
        m_entry += QLatin1Char('.');
}

void Backend::pressOperator(const QString &pretty) {
    if (m_errored)
        return;

    if (m_justEvaluated) {
        // Chain from the exact result value, not its rounded display text, so
        // 1 ÷ 3 = × 3 comes back as 1. Seventeen significant digits round-trip
        // any double; the expression line re-rounds them for presentation.
        m_tokens = QStringList(QString::number(m_resultValue, 'g', 17));
        clearEvaluation();
    }

    if (!m_entry.isEmpty()) {
        m_tokens << sealNumber(m_entry) << pretty;
        m_entry.clear();
    } else if (m_tokens.isEmpty() || m_tokens.last() == openParen) {
        // A group starts a fresh expression, so an operator leading one gets
        // the same implicit zero an operator leading the whole line gets.
        m_tokens << QStringLiteral("0") << pretty;
    } else if (isOperator(m_tokens.last())) {
        m_tokens.last() = pretty;
    } else {
        m_tokens << pretty;
    }
}

void Backend::pressEquals() {
    if (m_errored || m_justEvaluated)
        return;

    QStringList finalTokens = m_tokens;
    if (!m_entry.isEmpty())
        finalTokens << sealNumber(m_entry);
    while (!finalTokens.isEmpty() && isOperator(finalTokens.last()))
        finalTokens.removeLast();
    if (finalTokens.isEmpty())
        return;

    m_evaluatedExpression = prettyExpression(finalTokens);
    bool ok = false;
    const double value = evaluateTokens(finalTokens, &ok);
    if (!ok) {
        m_errored = true;
    } else {
        m_resultValue = value;
        m_result = formatNumber(value);
        m_justEvaluated = true;
    }
    m_tokens.clear();
    m_entry.clear();
}

// iOS-style percent: with a pending + or −, x% means x percent of the running
// total, so 200 + 10 % = gives 220. With × or ÷ (or on its own) x% is simply
// x ÷ 100, so 200 × 10 % = gives 20.
void Backend::pressPercent() {
    if (m_errored)
        return;

    if (m_justEvaluated) {
        const double percent = m_resultValue / 100.0;
        clearEvaluation();
        m_entry = formatNumber(percent);
        return;
    }

    bool ok = false;
    double value = 0;

    // Treat a just-closed group as the current operand. Remove its source
    // tokens so the calculated percentage can take its place without leaving
    // two adjacent operands, e.g. "(2 + 3) 0.05".
    if (m_entry.isEmpty() && !m_tokens.isEmpty() && m_tokens.last() == closeParen) {
        const int close = m_tokens.size() - 1;
        const int open = matchingOpenParen(m_tokens, close);
        if (open < 0)
            return;
        value = evaluateTokens(m_tokens.mid(open, close - open + 1), &ok);
        if (ok)
            m_tokens.erase(m_tokens.begin() + open, m_tokens.end());
    } else {
        value = QLocale::c().toDouble(currentValue(), &ok);
    }
    if (!ok)
        return;

    double percent = value / 100.0;
    if (!m_tokens.isEmpty() && (m_tokens.last() == plusSign || m_tokens.last() == minusSign)) {
        QStringList leftSide = m_tokens;
        leftSide.removeLast();
        leftSide = leftSide.mid(openGroupExpressionStart(leftSide));
        bool baseOk = false;
        const double base = evaluateTokens(leftSide, &baseOk);
        if (baseOk)
            percent = base * value / 100.0;
    }
    m_entry = formatNumber(percent);
}

void Backend::pressToggleSign() {
    if (m_errored)
        return;
    if (m_justEvaluated)
        beginEditingAfterResult();

    // With nothing typed yet, start a fresh negative operand instead of
    // dredging up the previous one: 4 + ± 2 enters -2, not -42.
    if (m_entry.isEmpty()) {
        m_entry = QStringLiteral("-0");
        return;
    }

    if (m_entry.startsWith(QLatin1Char('-')))
        m_entry.remove(0, 1);
    else
        m_entry.prepend(QLatin1Char('-'));
}

void Backend::pressBackspace() {
    if (m_errored) {
        pressClear();
        return;
    }
    if (m_justEvaluated)
        beginEditingAfterResult();

    m_entry.chop(1);
    if (m_entry == QStringLiteral("-"))
        m_entry.clear();
}

void Backend::pressClear() {
    m_tokens.clear();
    m_entry.clear();
    clearEvaluation();
    m_errored = false;
}

int Backend::openParenDepth() const {
    int depth = 0;
    for (const QString &token : m_tokens) {
        if (token == openParen)
            ++depth;
        else if (token == closeParen)
            --depth;
    }
    return depth;
}

// "(" is only valid where a fresh operand could start: at the beginning,
// right after an operator, or right after another "(". Anywhere else --
// mid-entry, right after a number, right after ")" -- it's ignored rather
// than guessing at an operator the user didn't type.
void Backend::pressOpenParen() {
    if (m_errored)
        pressClear();
    if (m_justEvaluated)
        pressClear();

    if (!m_entry.isEmpty())
        return;
    if (!m_tokens.isEmpty() && !isOperator(m_tokens.last()) && m_tokens.last() != openParen)
        return;

    m_tokens << openParen;
}

// ")" only closes a group that is actually open, and only over a complete
// value -- not a dangling operator or an empty "()".
void Backend::pressCloseParen() {
    if (m_errored || m_justEvaluated)
        return;
    if (openParenDepth() <= 0)
        return;

    if (!m_entry.isEmpty()) {
        m_tokens << sealNumber(m_entry);
        m_entry.clear();
        m_tokens << closeParen;
        return;
    }

    if (m_tokens.isEmpty() || isOperator(m_tokens.last()) || m_tokens.last() == openParen)
        return;
    m_tokens << closeParen;
}

// Editing after equals picks up from the result's displayed digits, with the
// old expression cleared away so the new one grows from "42" rather than
// "42 × 3 + 7".
void Backend::beginEditingAfterResult() {
    m_entry = m_result;
    m_tokens.clear();
    clearEvaluation();
}

void Backend::clearEvaluation() {
    m_result.clear();
    m_resultValue = 0;
    m_evaluatedExpression.clear();
    m_justEvaluated = false;
}

double Backend::evaluateTokens(const QStringList &tokens, bool *ok) {
    bool localOk = false;
    if (!ok)
        ok = &localOk;

    int pos = 0;
    *ok = true;
    const double total = parseExpression(tokens, pos, *ok);
    if (!*ok || pos != tokens.size() || !std::isfinite(total)) {
        *ok = false;
        return 0;
    }

    return total;
}

QString Backend::formatNumber(double value) {
    if (value == 0)
        value = 0;  // Collapse negative zero.

    // Fifteen significant digits keeps binary-float noise like
    // 0.1 + 0.2 = 0.30000000000000004 out of the display while showing
    // every integer the 15-digit entry limit can produce exactly; larger
    // magnitudes fall back to scientific notation.
    return QString::number(value, 'g', 15);
}

void Backend::copyResult() const {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(display());
}

// Paste replaces the current entry when the clipboard holds a number,
// tolerating surrounding whitespace, a decimal comma, and the typographic
// minus this calculator itself puts in expressions.
void Backend::pasteNumber() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    QString text = clipboard->text().trimmed();
    text.replace(minusSign, QStringLiteral("-"));
    text.remove(QLatin1Char(' '));

    bool ok = false;
    double value = QLocale::c().toDouble(text, &ok);
    if (!ok) {
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        value = QLocale::c().toDouble(text, &ok);
    }
    if (!ok || !std::isfinite(value))
        return;

    if (m_errored || m_justEvaluated)
        pressClear();
    m_entry = formatNumber(value);
    emit calculationChanged();
}

QVariantMap Backend::windowGeometry() const {
    const QSettings settings;
    const QRect geometry = settings.value(windowGeometrySetting).toRect();
    QVariantMap map;
    // Positions can legitimately be negative on monitors left of or above the
    // primary, so validity travels separately instead of being encoded as -1.
    map.insert(QStringLiteral("valid"), geometry.isValid());
    map.insert(QStringLiteral("x"), geometry.x());
    map.insert(QStringLiteral("y"), geometry.y());
    map.insert(QStringLiteral("width"), geometry.width());
    map.insert(QStringLiteral("height"), geometry.height());
    map.insert(QStringLiteral("maximized"),
               settings.value(QStringLiteral("window/maximized"), false).toBool());
    return map;
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    settings.setValue(windowGeometrySetting, QRect(x, y, width, height));
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}
