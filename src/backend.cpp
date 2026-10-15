#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>
#include <cmath>

namespace {
const QString plusSign = QStringLiteral("+");
const QString minusSign = QStringLiteral("−");
const QString multiplySign = QStringLiteral("×");
const QString divideSign = QStringLiteral("÷");
const QString powerSign = QStringLiteral("^");

// A leading # is a bare hex color rather than a comment.
QString withoutTomlComment(const QString &text) {
    const QString value = text.trimmed();
    QChar quote;
    bool escaped = false;
    for (int i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        if (escaped) {
            escaped = false;
        } else if (quote == QLatin1Char('"') && character == QLatin1Char('\\')) {
            escaped = true;
        } else if (!quote.isNull() && character == quote) {
            quote = QChar();
        } else if (quote.isNull() && (character == QLatin1Char('"')
                                      || character == QLatin1Char('\''))) {
            quote = character;
        } else if (quote.isNull() && character == QLatin1Char('#') && i > 0) {
            return value.left(i).trimmed();
        }
    }
    return value.trimmed();
}

bool isOperator(const QString &token) {
    return token == plusSign || token == minusSign
        || token == multiplySign || token == divideSign
        || token == powerSign;
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
    // Older builds stored the compositor's size. On Hyprland 0.56 that size
    // was the tiled or maximized rectangle, and the maximized flag was set for
    // a normal window, so the next launch filled the screen. Drop both keys.
    discardSavedWindowGeometry();
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

QString grouped(const QString &plain);

QString Backend::display() const {
    if (m_errored)
        return QStringLiteral("Error");
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_justEvaluated)
        return grouped(m_result);
    return grouped(formatNumber(QLocale::c().toDouble(currentValue())));
}

// Thousands separators on the result only. The expression and a copied value
// stay plain, so they paste back into the keypad.
QString grouped(const QString &plain) {
    if (plain.contains(QLatin1Char('e')) || plain.contains(QLatin1Char('E')))
        return plain;
    const bool negative = plain.startsWith(QLatin1Char('-'));
    const QString body = negative ? plain.mid(1) : plain;
    const int dot = body.indexOf(QLatin1Char('.'));
    const QString whole = dot < 0 ? body : body.left(dot);
    const QString fraction = dot < 0 ? QString() : body.mid(dot);
    if (whole.size() <= 3)
        return plain;
    QString out;
    for (int i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0)
            out += QLatin1Char(',');
        out += whole.at(i);
    }
    return (negative ? QStringLiteral("-") : QString()) + out + fraction;
}

// Operand tokens carry full round-trip precision when they come from a chained
// result; present every number at display precision instead.
QString Backend::prettyExpression(const QStringList &tokens) {
    QStringList pretty;
    pretty.reserve(tokens.size());
    for (const QString &token : tokens)
        pretty << (isOperator(token) ? token
                                     : formatNumber(QLocale::c().toDouble(token)));
    return pretty.join(QLatin1Char(' '));
}

// The number the calculator is "at" right now: the entry being typed, or the
// operand the last operator was applied to, or the fresh-start zero.
QString Backend::currentValue() const {
    if (!m_entry.isEmpty())
        return sealNumber(m_entry);
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (!isOperator(m_tokens.at(i)))
            return m_tokens.at(i);
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
    } else if (key == QStringLiteral("^")) {
        pressOperator(powerSign);
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
    } else if (m_tokens.isEmpty()) {
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
// total, so 200 + 10 % = gives 220. With ×, ÷, or ^ (or on its own) x% is
// simply x ÷ 100, so 200 × 10 % = gives 20.
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
    const double value = QLocale::c().toDouble(currentValue(), &ok);
    if (!ok)
        return;

    double percent = value / 100.0;
    if (!m_tokens.isEmpty() && (m_tokens.last() == plusSign || m_tokens.last() == minusSign)) {
        QStringList leftSide = m_tokens;
        leftSide.removeLast();
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

    *ok = false;
    if (tokens.size() % 2 == 0)
        return 0;

    QList<double> values;
    QStringList operators;

    bool numberOk = false;
    values << QLocale::c().toDouble(tokens.first(), &numberOk);
    if (!numberOk)
        return 0;

    for (int i = 1; i + 1 < tokens.size(); i += 2) {
        const QString &op = tokens.at(i);
        const double operand = QLocale::c().toDouble(tokens.at(i + 1), &numberOk);
        if (!numberOk || !isOperator(op))
            return 0;
        operators << op;
        values << operand;
    }

    // Fold ^ right-to-left so 2 ^ 3 ^ 2 is 2^(3^2), not (2^3)^2. Then fold ×
    // and ÷ left-to-right, then + and −.
    for (int i = operators.size() - 1; i >= 0; --i) {
        if (operators.at(i) != powerSign)
            continue;

        const double base = values.at(i);
        const double exponent = values.at(i + 1);
        // A negative base with a fractional exponent has no real result.
        if (base < 0.0 && std::floor(exponent) != exponent)
            return 0;

        const double powered = std::pow(base, exponent);
        if (!std::isfinite(powered))
            return 0;

        values[i] = powered;
        values.removeAt(i + 1);
        operators.removeAt(i);
    }

    QList<double> additiveValues;
    QStringList additiveOperators;
    additiveValues << values.first();
    for (int i = 0; i < operators.size(); ++i) {
        const QString &op = operators.at(i);
        const double operand = values.at(i + 1);
        if (op == multiplySign) {
            additiveValues.last() *= operand;
        } else if (op == divideSign) {
            additiveValues.last() /= operand;
        } else {
            additiveOperators << op;
            additiveValues << operand;
        }
    }

    double total = additiveValues.first();
    for (int i = 0; i < additiveOperators.size(); ++i) {
        if (additiveOperators.at(i) == plusSign)
            total += additiveValues.at(i + 1);
        else
            total -= additiveValues.at(i + 1);
    }

    if (!std::isfinite(total))
        return 0;

    *ok = true;
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
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        const QString plain = m_justEvaluated ? m_result
            : (!m_entry.isEmpty() ? m_entry : formatNumber(QLocale::c().toDouble(currentValue())));
        clipboard->setText(plain);
    }
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

QVariantMap Backend::defaultWindowSize() const {
    qreal scale = m_textScale;
    if (!std::isfinite(scale) || scale <= 0)
        scale = 1.0;

    // 400×568 is the v0.2.2 design size. The minimum matches Main.qml, so a
    // text scale below 1 still opens at a usable calculator.
    QVariantMap size;
    size.insert(QStringLiteral("width"), qMax(300, qRound(400 * scale)));
    size.insert(QStringLiteral("height"), qMax(430, qRound(568 * scale)));
    return size;
}

void Backend::discardSavedWindowGeometry() {
    QSettings settings;
    const bool hadGeometry = settings.contains(QStringLiteral("window/geometry"));
    const bool hadMaximized = settings.contains(QStringLiteral("window/maximized"));
    if (!hadGeometry && !hadMaximized)
        return;

    settings.remove(QStringLiteral("window/geometry"));
    settings.remove(QStringLiteral("window/maximized"));
    settings.sync();
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
    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QString themeBackground;
    QString themeForeground;
    QString themeAccent;
    QString themeSelection;
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
            QString value = withoutTomlComment(line.mid(equals + 1));
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background") && QColor(value).isValid())
                themeBackground = value;
            else if (key == QStringLiteral("foreground") && QColor(value).isValid())
                themeForeground = value;
            else if (key == QStringLiteral("accent") && QColor(value).isValid())
                themeAccent = value;
            else if (key == QStringLiteral("selection") && QColor(value).isValid())
                themeSelection = value;
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
        const QColor background(themeBackground);
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

    m_themeBackground = themeBackground.isEmpty()
        ? (m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff")) : themeBackground;
    m_themeForeground = themeForeground.isEmpty()
        ? (m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324")) : themeForeground;
    m_themeAccent = themeAccent.isEmpty()
        ? (m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2")) : themeAccent;
    m_themeSelection = themeSelection.isEmpty()
        ? (m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2")) : themeSelection;

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
