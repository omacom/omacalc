#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
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

// A result, percent, or paste too large or small for plain digits arrives as
// 1e-07. It is a whole value: typing must not edit its exponent.
bool inExponentForm(const QString &entry) {
    return entry.contains(QLatin1Char('e'));
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

QString Backend::display() const {
    return groupDigits(plainDisplay());
}

// The display without thousands separators, which is what copy hands to other
// apps: a spreadsheet or terminal reads 1234567 more reliably than 1,234,567.
QString Backend::plainDisplay() const {
    if (m_errored)
        return tr("Error");
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_justEvaluated)
        return m_result;
    return currentValue().toString();
}

std::optional<Number> Backend::value() const {
    if (m_errored)
        return std::nullopt;
    if (m_justEvaluated)
        return m_resultValue;
    return currentValue();
}

// Operands are exact, so a chained 1 ÷ 3 is held as one third; present every
// number at display precision instead.
QString Backend::prettyExpression(const QList<Token> &tokens) {
    QStringList pretty;
    pretty.reserve(tokens.size());
    for (const Token &token : tokens)
        pretty << (token.isOperator() ? token.op : groupDigits(token.number.toString()));
    return pretty.join(QLatin1Char(' '));
}

// The number the calculator is "at" right now: the entry being typed, or the
// operand the last operator was applied to, or the fresh-start zero.
Number Backend::currentValue() const {
    if (!m_entry.isEmpty())
        return m_entryValue;
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (!m_tokens.at(i).isOperator())
            return m_tokens.at(i).number;
    }
    return Number();
}

// Typing makes the text the number: "5." is 5 and "-0" is 0.
void Backend::setEntryText(const QString &text) {
    m_entry = text;
    m_entryValue = Number::parse(sealNumber(text)).value_or(Number());
}

void Backend::setEntryValue(const Number &number) {
    m_entry = number.toString();
    m_entryValue = number;
}

void Backend::clearEntry() {
    m_entry.clear();
    m_entryValue = Number();
}

void Backend::pressKey(const QString &key) {
    // ASCII digits only: QChar::isDigit() also takes ٣ and other scripts'
    // digits, which would sit in the entry as text the math reads as 0.
    if (key.size() == 1 && key.at(0) >= QLatin1Char('0') && key.at(0) <= QLatin1Char('9')) {
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

    QString entry = inExponentForm(m_entry) ? QString() : m_entry;
    if (entry == QStringLiteral("0")) {
        entry = digit;
    } else if (entry == QStringLiteral("-0")) {
        entry = QStringLiteral("-") + digit;
    } else {
        // Fifteen significant digits is what the display shows; letting the
        // entry grow beyond that would hide what was typed. The zero in a
        // leading "0." is not significant, so it does not count.
        int digits = 0;
        for (const QChar character : entry) {
            if (character.isDigit())
                ++digits;
        }
        if (entry.startsWith(QStringLiteral("0."))
                || entry.startsWith(QStringLiteral("-0.")))
            --digits;
        if (digits < Number::kDisplayDigits)
            entry += digit;
    }
    if (entry != m_entry)
        setEntryText(entry);
}

void Backend::pressDecimal() {
    if (m_errored)
        pressClear();
    if (m_justEvaluated)
        pressClear();

    QString entry = inExponentForm(m_entry) ? QString() : m_entry;
    if (entry.isEmpty())
        entry = QStringLiteral("0.");
    else if (!entry.contains(QLatin1Char('.')))
        entry += QLatin1Char('.');
    if (entry != m_entry)
        setEntryText(entry);
}

void Backend::pressOperator(const QString &pretty) {
    if (m_errored)
        return;

    if (m_justEvaluated) {
        // Chain from the exact result, not its rounded display text, so
        // 1 ÷ 3 = × 3 comes back as exactly 1.
        m_tokens = {Token::value(m_resultValue)};
        clearEvaluation();
    }

    if (!m_entry.isEmpty()) {
        m_tokens << Token::value(m_entryValue) << Token::operation(pretty);
        clearEntry();
    } else if (m_tokens.isEmpty()) {
        m_tokens << Token::value(Number()) << Token::operation(pretty);
    } else if (m_tokens.last().isOperator()) {
        m_tokens.last().op = pretty;
    } else {
        m_tokens << Token::operation(pretty);
    }
}

void Backend::pressEquals() {
    if (m_errored || m_justEvaluated)
        return;

    QList<Token> finalTokens = m_tokens;
    if (!m_entry.isEmpty())
        finalTokens << Token::value(m_entryValue);
    while (!finalTokens.isEmpty() && finalTokens.last().isOperator())
        finalTokens.removeLast();
    if (finalTokens.isEmpty())
        return;

    m_evaluatedExpression = prettyExpression(finalTokens);
    const std::optional<Number> result = evaluate(finalTokens);
    if (!result) {
        m_errored = true;
    } else {
        m_resultValue = *result;
        m_result = result->toString();
        m_justEvaluated = true;
    }
    m_tokens.clear();
    clearEntry();
}

// iOS-style percent: with a pending + or −, x% means x percent of the running
// total, so 200 + 10 % = gives 220. With ×, ÷, or ^ (or on its own) x% is
// simply x ÷ 100, so 200 × 10 % = gives 20.
void Backend::pressPercent() {
    if (m_errored)
        return;

    const Number value = m_justEvaluated ? m_resultValue : currentValue();
    if (m_justEvaluated)
        clearEvaluation();

    std::optional<Number> percent = Number::percent(value);
    if (!m_tokens.isEmpty() && (m_tokens.last().op == plusSign || m_tokens.last().op == minusSign)) {
        const std::optional<Number> total = evaluate(m_tokens.mid(0, m_tokens.size() - 1));
        if (total)
            percent = Number::percent(value, *total);
    }
    if (!percent) {
        m_evaluatedExpression = prettyExpression(m_tokens + QList<Token>{Token::value(value)})
            + QStringLiteral(" %");
        m_errored = true;
        m_tokens.clear();
        clearEntry();
        return;
    }
    setEntryValue(*percent);
}

void Backend::pressToggleSign() {
    if (m_errored)
        return;
    if (m_justEvaluated)
        beginEditingAfterResult();

    // With nothing typed yet, start a fresh negative operand instead of
    // dredging up the previous one: 4 + ± 2 enters -2, not -42.
    if (m_entry.isEmpty()) {
        setEntryText(QStringLiteral("-0"));
        return;
    }

    m_entryValue = m_entryValue.negated();
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
    if (inExponentForm(m_entry)) {
        setEntryText(QStringLiteral("0"));
        return;
    }

    QString entry = m_entry;
    entry.chop(1);
    if (entry == QStringLiteral("-"))
        entry.clear();
    if (entry.isEmpty())
        clearEntry();
    else
        setEntryText(entry);
}

void Backend::pressClear() {
    m_tokens.clear();
    clearEntry();
    clearEvaluation();
    m_errored = false;
}

// Editing after equals picks up from the result's displayed digits, with the
// old expression cleared away so the new one grows from "42" rather than
// "42 × 3 + 7". The exact result stays behind the digits until they change,
// so ± on 1 ÷ 3 = is exactly minus one third.
void Backend::beginEditingAfterResult() {
    m_entry = m_result;
    m_entryValue = m_resultValue;
    m_tokens.clear();
    clearEvaluation();
}

void Backend::clearEvaluation() {
    m_result.clear();
    m_resultValue = Number();
    m_evaluatedExpression.clear();
    m_justEvaluated = false;
}

std::optional<Number> Backend::evaluate(const QList<Token> &tokens) {
    if (tokens.size() % 2 == 0)
        return std::nullopt;

    QList<Number> values;
    QStringList operators;
    for (int i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens.at(i);
        const bool operatorExpected = i % 2 == 1;
        if (token.isOperator() != operatorExpected)
            return std::nullopt;
        if (operatorExpected) {
            if (!isOperator(token.op))
                return std::nullopt;
            operators << token.op;
        } else {
            values << token.number;
        }
    }

    // Fold ^ right-to-left so 2 ^ 3 ^ 2 is 2^(3^2), not (2^3)^2. Then fold ×
    // and ÷ left-to-right, then + and −.
    for (int i = operators.size() - 1; i >= 0; --i) {
        if (operators.at(i) != powerSign)
            continue;

        const std::optional<Number> powered = Number::power(values.at(i), values.at(i + 1));
        if (!powered)
            return std::nullopt;

        values[i] = *powered;
        values.removeAt(i + 1);
        operators.removeAt(i);
    }

    QList<Number> terms;
    QStringList termOperators;
    terms << values.first();
    for (int i = 0; i < operators.size(); ++i) {
        const QString &op = operators.at(i);
        const Number &operand = values.at(i + 1);
        if (op == multiplySign || op == divideSign) {
            const std::optional<Number> product = op == multiplySign
                ? Number::multiply(terms.last(), operand)
                : Number::divide(terms.last(), operand);
            if (!product)
                return std::nullopt;
            terms.last() = *product;
        } else {
            termOperators << op;
            terms << operand;
        }
    }

    Number total = terms.first();
    for (int i = 0; i < termOperators.size(); ++i) {
        const std::optional<Number> sum = termOperators.at(i) == plusSign
            ? Number::add(total, terms.at(i + 1))
            : Number::subtract(total, terms.at(i + 1));
        if (!sum)
            return std::nullopt;
        total = *sum;
    }
    return total;
}

// Separates the whole-number digits into groups of three with commas, so
// 1234567.891 reads as 1,234,567.891. Only presentation goes through here:
// tokens, entries, and results stay plain so parsing and chaining never see a
// separator. The fraction and any exponent are left alone, and text that is
// not a number, like "Error", passes through unchanged.
QString Backend::groupDigits(const QString &number) {
    const int start = number.startsWith(QLatin1Char('-')) ? 1 : 0;
    int end = start;
    while (end < number.size() && number.at(end).isDigit())
        ++end;
    if (end - start <= 3)
        return number;

    QString grouped = number;
    for (int i = end - 3; i > start; i -= 3)
        grouped.insert(i, QLatin1Char(','));
    return grouped;
}

void Backend::copyResult() const {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(plainDisplay());
}

// Paste replaces the current entry when the clipboard holds a number,
// tolerating surrounding whitespace, thousands separators, a decimal comma,
// and the typographic minus this calculator itself puts in expressions. The
// display shows fifteen digits of it, and the calculation uses every digit.
void Backend::pasteNumber() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    QString text = clipboard->text().trimmed();
    text.replace(minusSign, QStringLiteral("-"));
    text.remove(QLatin1Char(' '));

    // Commas in groups of exactly three digits are thousands separators, the
    // way this calculator shows them. A short tail like "1,5" does not match
    // and still reads as a decimal comma below.
    static const QRegularExpression grouped(
        QStringLiteral("^-?\\d{1,3}(,\\d{3})+(\\.\\d*)?$"));
    if (grouped.match(text).hasMatch())
        text.remove(QLatin1Char(','));

    std::optional<Number> value = Number::parse(text);
    if (!value) {
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        value = Number::parse(text);
    }
    if (!value)
        return;

    if (m_errored || m_justEvaluated)
        pressClear();
    setEntryValue(*value);
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
