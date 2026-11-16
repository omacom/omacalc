#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
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
const QString openParenthesis = QStringLiteral("(");
const QString closeParenthesis = QStringLiteral(")");

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

const QStringList functionKeys = {
    QStringLiteral("sin"), QStringLiteral("cos"), QStringLiteral("tan"),
    QStringLiteral("asin"), QStringLiteral("acos"), QStringLiteral("atan"),
    QStringLiteral("ln"), QStringLiteral("log"), QStringLiteral("exp"),
    QStringLiteral("sqrt"), QStringLiteral("square"), QStringLiteral("cube"),
    QStringLiteral("reciprocal"), QStringLiteral("factorial"),
};

// How a failed function reads back on the expression line: √(-4), 1/(0), 3.5!
QString describeFunction(const QString &function, const QString &operand) {
    if (function == QStringLiteral("factorial"))
        return operand + QLatin1Char('!');
    if (function == QStringLiteral("sqrt"))
        return QStringLiteral("√(") + operand + QLatin1Char(')');
    if (function == QStringLiteral("reciprocal"))
        return QStringLiteral("1/(") + operand + QLatin1Char(')');
    if (function == QStringLiteral("exp"))
        return QStringLiteral("e^(") + operand + QLatin1Char(')');
    return function + QLatin1Char('(') + operand + QLatin1Char(')');
}

bool isParenthesis(const QString &token) {
    return token == openParenthesis || token == closeParenthesis;
}

// Recursive descent over the token list: + and − bind loosest, then × and ÷,
// then ^ (right to left), with parentheses grouping anywhere a number can go.
class TokenParser {
public:
    explicit TokenParser(const QStringList &tokens) : m_tokens(tokens) {}

    double parse(bool *ok) {
        const double value = sum();
        *ok = m_ok && m_position == m_tokens.size() && std::isfinite(value);
        return *ok ? value : 0;
    }

private:
    bool at(const QString &token) const {
        return m_position < m_tokens.size() && m_tokens.at(m_position) == token;
    }

    double sum() {
        double total = product();
        while (m_ok && (at(plusSign) || at(minusSign))) {
            const bool adding = at(plusSign);
            ++m_position;
            const double operand = product();
            total = adding ? total + operand : total - operand;
            if (!std::isfinite(total))
                return fail();
        }
        return total;
    }

    double product() {
        double total = power();
        while (m_ok && (at(multiplySign) || at(divideSign))) {
            const bool multiplying = at(multiplySign);
            ++m_position;
            const double operand = power();
            // Fail here rather than at the end: a later step like ^ 0 would
            // otherwise turn the infinity back into an ordinary number.
            if (!multiplying && operand == 0)
                return fail();
            total = multiplying ? total * operand : total / operand;
            if (!std::isfinite(total))
                return fail();
        }
        return total;
    }

    double power() {
        const double base = operand();
        if (!m_ok || !at(powerSign))
            return base;
        ++m_position;
        const double exponent = power();
        // A negative base with a fractional exponent has no real result.
        if (base < 0.0 && std::floor(exponent) != exponent)
            return fail();
        const double powered = std::pow(base, exponent);
        return std::isfinite(powered) ? powered : fail();
    }

    double operand() {
        if (m_position >= m_tokens.size())
            return fail();
        if (at(openParenthesis)) {
            ++m_position;
            const double value = sum();
            if (!m_ok || !at(closeParenthesis))
                return fail();
            ++m_position;
            return value;
        }
        bool numberOk = false;
        const double value = QLocale::c().toDouble(m_tokens.at(m_position), &numberOk);
        if (!numberOk)
            return fail();
        ++m_position;
        return value;
    }

    double fail() {
        m_ok = false;
        return 0;
    }

    const QStringList &m_tokens;
    int m_position = 0;
    bool m_ok = true;
};

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
    QSettings settings;
    m_scientific = settings.value(QStringLiteral("scientific"), false).toBool();
    m_degrees = settings.value(QStringLiteral("degrees"), true).toBool();
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
    // A computed entry carries every digit for the next step; show it at
    // display precision like any result.
    if (m_entryComputed)
        return formatNumber(QLocale::c().toDouble(m_entry));
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_justEvaluated)
        return m_result;
    return formatNumber(QLocale::c().toDouble(currentValue()));
}

// Operand tokens carry full round-trip precision when they come from a chained
// result; present every number at display precision instead.
QString Backend::prettyExpression(const QStringList &tokens) {
    // Spaces around operators, none just inside parentheses: 2 × (3 + 4).
    QString pretty;
    QString previous;
    for (const QString &token : tokens) {
        if (!pretty.isEmpty() && previous != openParenthesis && token != closeParenthesis)
            pretty += QLatin1Char(' ');
        pretty += (isOperator(token) || isParenthesis(token))
            ? token
            : groupDigits(formatNumber(QLocale::c().toDouble(token)));
        previous = token;
    }
    return pretty;
}

// The number the calculator is "at" right now: the entry being typed, or the
// operand the last operator was applied to, or the fresh-start zero.
QString Backend::currentValue() const {
    if (!m_entry.isEmpty())
        return sealNumber(m_entry);
    // After ")" the calculator is at the group's value, not its last number.
    const int group = closedGroupStart();
    if (group >= 0) {
        bool ok = false;
        const double value = evaluateTokens(m_tokens.mid(group), &ok);
        if (ok)
            return QString::number(value, 'g', 17);
    }
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (!isOperator(m_tokens.at(i)) && !isParenthesis(m_tokens.at(i)))
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
    } else if (key == openParenthesis) {
        pressOpenParenthesis();
    } else if (key == closeParenthesis) {
        pressCloseParenthesis();
    } else if (key == QStringLiteral("pi") || key == QStringLiteral("π")) {
        pressConstant(M_PI);
    } else if (key == QStringLiteral("e")) {
        pressConstant(M_E);
    } else if (key == QStringLiteral("angle")) {
        setDegrees(!m_degrees);
        return;
    } else if (key == QStringLiteral("scientific")) {
        setScientific(!m_scientific);
        return;
    } else if (functionKeys.contains(key)) {
        pressFunction(key);
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
    if (inExponentForm(m_entry) || m_entryComputed)
        m_entry.clear();
    m_entryComputed = false;

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
    if (inExponentForm(m_entry) || m_entryComputed)
        m_entry.clear();
    m_entryComputed = false;

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
        pushEntry();
        m_tokens << pretty;
    } else if (m_tokens.isEmpty() || m_tokens.last() == openParenthesis) {
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

    pushEntry();
    QStringList finalTokens = m_tokens;
    // Drop a dangling operator or an empty "(", then close what is still open.
    while (!finalTokens.isEmpty()
           && (isOperator(finalTokens.last()) || finalTokens.last() == openParenthesis))
        finalTokens.removeLast();
    if (finalTokens.isEmpty()) {
        m_tokens.clear();
        return;
    }
    int depth = 0;
    for (const QString &token : finalTokens)
        depth += token == openParenthesis ? 1 : (token == closeParenthesis ? -1 : 0);
    for (; depth > 0; --depth)
        finalTokens << closeParenthesis;

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
        m_entry = QString::number(percent, 'g', 17);
        m_entryComputed = true;
        return;
    }

    if (m_entry.isEmpty() && !m_tokens.isEmpty() && m_tokens.last() == openParenthesis)
        return;

    // A closed group is a single value: (2 + 3) % is 0.05.
    double value = 0;
    if (m_entry.isEmpty() && closedGroupStart() >= 0) {
        QString shown;
        if (!takeClosedGroup(&value, &shown)) {
            m_evaluatedExpression = prettyExpression(m_tokens) + QStringLiteral(" %");
            m_errored = true;
            m_tokens.clear();
            return;
        }
    } else {
        bool ok = false;
        value = QLocale::c().toDouble(currentValue(), &ok);
        if (!ok)
            return;
    }

    double percent = value / 100.0;
    if (!m_tokens.isEmpty() && (m_tokens.last() == plusSign || m_tokens.last() == minusSign)) {
        // The running total is the one inside the innermost open parenthesis.
        QStringList leftSide = m_tokens.mid(currentGroupStart());
        leftSide.removeLast();
        bool baseOk = false;
        const double base = evaluateTokens(leftSide, &baseOk);
        if (baseOk) {
            percent = base * value / 100.0;
            // 1e308 + 100 % overflows multiplying first; dividing first
            // would round a tiny percentage, so only fall back to it.
            if (!std::isfinite(percent))
                percent = base * (value / 100.0);
        }
    }
    if (!std::isfinite(percent)) {
        m_evaluatedExpression = prettyExpression(m_tokens + QStringList(QString::number(value, 'g', 17)))
            + QStringLiteral(" %");
        m_errored = true;
        m_tokens.clear();
        m_entry.clear();
        return;
    }
    // Keep every digit for the next step; the display rounds it.
    m_entry = QString::number(percent, 'g', 17);
    m_entryComputed = true;
}

void Backend::pressToggleSign() {
    if (m_errored)
        return;
    if (m_justEvaluated)
        beginEditingAfterResult();

    // With nothing typed yet, start a fresh negative operand instead of
    // dredging up the previous one: 4 + ± 2 enters -2, not -42.
    // After ")" the sign belongs to the whole group: (2 + 3) ± is -5.
    if (m_entry.isEmpty() && closedGroupStart() >= 0) {
        double value = 0;
        QString shown;
        if (!takeClosedGroup(&value, &shown)) {
            m_evaluatedExpression = prettyExpression(m_tokens) + QStringLiteral(" ±");
            m_errored = true;
            m_tokens.clear();
            return;
        }
        m_entry = QString::number(-value, 'g', 17);
        m_entryComputed = true;
        return;
    }
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
    if (inExponentForm(m_entry) || m_entryComputed) {
        m_entry = QStringLiteral("0");
        m_entryComputed = false;
        return;
    }

    // With nothing typed, backspace takes back a parenthesis. Removing a ")"
    // hands the number before it back to the entry for editing.
    if (m_entry.isEmpty() && !m_tokens.isEmpty() && isParenthesis(m_tokens.last())) {
        m_tokens.removeLast();
        if (!m_tokens.isEmpty() && !isOperator(m_tokens.last())
                && !isParenthesis(m_tokens.last()))
            m_entry = m_tokens.takeLast();
        return;
    }

    m_entry.chop(1);
    if (m_entry == QStringLiteral("-"))
        m_entry.clear();
}

void Backend::pressClear() {
    m_tokens.clear();
    m_entry.clear();
    m_entryComputed = false;
    clearEvaluation();
    m_errored = false;
}

// Editing after equals picks up from the result's displayed digits, with the
// old expression cleared away so the new one grows from "42" rather than
// "42 × 3 + 7".
void Backend::beginEditingAfterResult() {
    m_entry = m_result;
    m_entryComputed = false;
    m_tokens.clear();
    clearEvaluation();
}

// Seals the number being typed into the expression. A number straight after
// ")" multiplies the group, so (2 + 3) 4 reads as (2 + 3) × 4.
void Backend::pushEntry() {
    if (m_entry.isEmpty())
        return;
    if (!m_tokens.isEmpty() && m_tokens.last() == closeParenthesis)
        m_tokens << multiplySign;
    m_tokens << sealNumber(m_entry);
    m_entry.clear();
    m_entryComputed = false;
}

int Backend::openParentheses() const {
    int depth = 0;
    for (const QString &token : m_tokens)
        depth += token == openParenthesis ? 1 : (token == closeParenthesis ? -1 : 0);
    return depth;
}

// Index of the first token inside the innermost parenthesis still open, or 0.
int Backend::currentGroupStart() const {
    int depth = 0;
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (m_tokens.at(i) == closeParenthesis) {
            ++depth;
        } else if (m_tokens.at(i) == openParenthesis) {
            if (depth == 0)
                return i + 1;
            --depth;
        }
    }
    return 0;
}

// Index of the "(" matching a trailing ")", or -1 when the tokens do not end
// in a closed group.
int Backend::closedGroupStart() const {
    if (m_tokens.isEmpty() || m_tokens.last() != closeParenthesis)
        return -1;
    int depth = 0;
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (m_tokens.at(i) == closeParenthesis)
            ++depth;
        else if (m_tokens.at(i) == openParenthesis && --depth == 0)
            return i;
    }
    return -1;
}

// Evaluates the trailing closed group into *value and, when it is valid,
// removes it from the tokens so the caller can replace it with an entry.
// *shown is the group as the expression line reads it, for error messages.
bool Backend::takeClosedGroup(double *value, QString *shown) {
    const int start = closedGroupStart();
    if (start < 0)
        return false;
    const QStringList group = m_tokens.mid(start);
    bool ok = false;
    *value = evaluateTokens(group, &ok);
    *shown = prettyExpression(group);
    if (ok)
        m_tokens = m_tokens.mid(0, start);
    return ok;
}

// A number already typed, or a closed group, multiplies what the parenthesis
// opens: 2 ( 3 + 4 ) is 2 × (3 + 4).
void Backend::pressOpenParenthesis() {
    if (m_errored || m_justEvaluated)
        pressClear();
    if (!m_entry.isEmpty()) {
        pushEntry();
        m_tokens << multiplySign;
    } else if (!m_tokens.isEmpty() && m_tokens.last() == closeParenthesis) {
        m_tokens << multiplySign;
    }
    m_tokens << openParenthesis;
}

void Backend::pressCloseParenthesis() {
    if (m_errored || m_justEvaluated || openParentheses() == 0)
        return;
    pushEntry();
    while (!m_tokens.isEmpty() && isOperator(m_tokens.last()))
        m_tokens.removeLast();
    // An empty pair closes nothing.
    if (m_tokens.isEmpty() || m_tokens.last() == openParenthesis)
        return;
    m_tokens << closeParenthesis;
}

// π and e replace whatever is being typed, like a paste.
void Backend::pressConstant(double value) {
    if (m_errored || m_justEvaluated)
        pressClear();
    m_entry = QString::number(value, 'g', 17);
    m_entryComputed = true;
}

// Functions apply at once to the number on screen, the way a pocket
// scientific calculator works: 9 √ shows 3. After ")" they apply to the whole
// group, and after = to the result.
void Backend::pressFunction(const QString &function) {
    if (m_errored)
        return;

    double value = 0;
    QString shown;
    if (m_justEvaluated) {
        value = m_resultValue;
        shown = m_result;
        clearEvaluation();
    } else if (m_entry.isEmpty() && closedGroupStart() >= 0) {
        if (!takeClosedGroup(&value, &shown))
            value = qQNaN();
    } else {
        shown = currentValue();
        value = QLocale::c().toDouble(shown);
        shown = groupDigits(formatNumber(value));
    }

    bool ok = false;
    const double result = std::isnan(value) ? 0 : applyFunction(function, value, m_degrees, &ok);
    if (!ok) {
        m_evaluatedExpression = prettyExpression(m_tokens);
        if (!m_evaluatedExpression.isEmpty())
            m_evaluatedExpression += QLatin1Char(' ');
        m_evaluatedExpression += describeFunction(function, shown);
        m_errored = true;
        m_tokens.clear();
        m_entry.clear();
        m_entryComputed = false;
        return;
    }
    m_entry = QString::number(result, 'g', 17);
    m_entryComputed = true;
}

// Sets *ok false for an unknown function or a value outside its domain.
double Backend::applyFunction(const QString &function, double value, bool degrees, bool *ok) {
    bool localOk = false;
    if (!ok)
        ok = &localOk;
    *ok = false;

    const double toRadians = degrees ? M_PI / 180.0 : 1.0;
    double result = qQNaN();

    if (function == QStringLiteral("sin") || function == QStringLiteral("cos")
            || function == QStringLiteral("tan")) {
        const bool sine = function == QStringLiteral("sin");
        const bool cosine = function == QStringLiteral("cos");
        // Whole multiples of 90° are exact, so sin 180 is 0 and tan 90 is
        // an error rather than 1.2e-16 and 1.6e16.
        if (degrees && std::isfinite(value) && std::fmod(value, 90.0) == 0) {
            const int quarter = int(std::fmod(std::fmod(value / 90.0, 4.0) + 4.0, 4.0));
            static const int sines[] = {0, 1, 0, -1};
            const int s = sines[quarter];
            const int c = sines[(quarter + 1) % 4];
            if (sine)
                result = s;
            else if (cosine)
                result = c;
            else if (c != 0)
                result = double(s) / c;
        } else {
            const double angle = value * toRadians;
            result = sine ? std::sin(angle) : (cosine ? std::cos(angle) : std::tan(angle));
            // Rounding noise at a zero crossing, like sin π, but not the
            // genuinely tiny sine of a tiny angle.
            if (std::fabs(result) < 1e-15 && std::fabs(angle) > 1e-6)
                result = 0;
        }
    } else if (function == QStringLiteral("asin")) {
        if (value >= -1 && value <= 1)
            result = std::asin(value) / toRadians;
    } else if (function == QStringLiteral("acos")) {
        if (value >= -1 && value <= 1)
            result = std::acos(value) / toRadians;
    } else if (function == QStringLiteral("atan")) {
        result = std::atan(value) / toRadians;
    } else if (function == QStringLiteral("ln")) {
        if (value > 0)
            result = std::log(value);
    } else if (function == QStringLiteral("log")) {
        if (value > 0)
            result = std::log10(value);
    } else if (function == QStringLiteral("exp")) {
        result = std::exp(value);
    } else if (function == QStringLiteral("sqrt")) {
        if (value >= 0)
            result = std::sqrt(value);
    } else if (function == QStringLiteral("square")) {
        result = value * value;
    } else if (function == QStringLiteral("cube")) {
        result = value * value * value;
    } else if (function == QStringLiteral("reciprocal")) {
        if (value != 0)
            result = 1.0 / value;
    } else if (function == QStringLiteral("factorial")) {
        // Whole numbers multiply out exactly; fractions use Γ(x + 1).
        if (value >= 0 && std::floor(value) == value) {
            result = 1;
            for (double i = 2; i <= value && std::isfinite(result); ++i)
                result *= i;
        } else if (value > -1 || std::floor(value) != value) {
            result = std::tgamma(value + 1);
        }
    }

    if (!std::isfinite(result))
        return 0;
    *ok = true;
    return result;
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
    if (tokens.isEmpty()) {
        *ok = false;
        return 0;
    }
    return TokenParser(tokens).parse(ok);
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
// and the typographic minus this calculator itself puts in expressions.
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
    m_entryComputed = false;
    emit calculationChanged();
}

QVariantMap Backend::defaultWindowSize() const {
    qreal scale = m_textScale;
    if (!std::isfinite(scale) || scale <= 0)
        scale = 1.0;

    // 400×568 is the v0.2.2 design size. The minimum matches Main.qml, so a
    // text scale below 1 still opens at a usable calculator.
    QVariantMap size;
    // Scientific mode adds four key columns, doubling the design width.
    const int designWidth = m_scientific ? 800 : 400;
    size.insert(QStringLiteral("width"), qMax(designWidth * 3 / 4, qRound(designWidth * scale)));
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

void Backend::setScientific(bool scientific) {
    if (m_scientific == scientific)
        return;

    m_scientific = scientific;
    QSettings().setValue(QStringLiteral("scientific"), scientific);
    emit scientificChanged();
}

void Backend::setDegrees(bool degrees) {
    if (m_degrees == degrees)
        return;

    m_degrees = degrees;
    QSettings().setValue(QStringLiteral("degrees"), degrees);
    emit degreesChanged();
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
