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

bool isOperator(const QString &token) {
    return token == plusSign || token == minusSign
        || token == multiplySign || token == divideSign;
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
    m_rpnMode = QSettings().value(QStringLiteral("calculator/rpnMode"), false).toBool();
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
    if (m_rpnMode)
        return rpnExpression();
    if (m_errored || m_justEvaluated)
        return m_evaluatedExpression;
    return prettyExpression(m_tokens);
}

QString Backend::display() const {
    if (m_errored)
        return QStringLiteral("Error");
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_rpnMode) {
        if (!m_stack.isEmpty())
            return formatNumber(QLocale::c().toDouble(m_stack.last()));
        return QStringLiteral("0");
    }
    if (m_justEvaluated)
        return m_result;
    return formatNumber(QLocale::c().toDouble(currentValue()));
}

QString Backend::rpnExpression() const {
    if (m_errored)
        return m_evaluatedExpression;

    QString expression = prettyExpression(m_stack);
    if (m_entry.isEmpty())
        return expression;
    if (expression.isEmpty())
        return m_entry;
    return expression + QLatin1Char(' ') + m_entry;
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
    } else if (key == QStringLiteral("swap")) {
        pressSwap();
    } else {
        return;
    }

    emit calculationChanged();
}

void Backend::pressDigit(const QString &digit) {
    if (m_errored)
        pressClear();
    if (m_justEvaluated) {
        // Infix: a digit after equals starts a new calculation rather than
        // appending to the result. RPN keeps the stack and starts a new entry.
        if (m_rpnMode)
            m_justEvaluated = false;
        else
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
    if (m_justEvaluated) {
        if (m_rpnMode)
            m_justEvaluated = false;
        else
            pressClear();
    }

    if (m_entry.isEmpty())
        m_entry = QStringLiteral("0.");
    else if (!m_entry.contains(QLatin1Char('.')))
        m_entry += QLatin1Char('.');
}

void Backend::pressOperator(const QString &pretty) {
    if (m_errored)
        return;

    if (m_rpnMode) {
        pushEntry();
        if (m_stack.size() < 2)
            return;

        bool rightOk = false;
        bool leftOk = false;
        const double right = QLocale::c().toDouble(m_stack.takeLast(), &rightOk);
        const double left = QLocale::c().toDouble(m_stack.takeLast(), &leftOk);
        if (!rightOk || !leftOk) {
            m_errored = true;
            m_evaluatedExpression.clear();
            return;
        }

        double value = 0;
        if (pretty == plusSign)
            value = left + right;
        else if (pretty == minusSign)
            value = left - right;
        else if (pretty == multiplySign)
            value = left * right;
        else if (pretty == divideSign)
            value = left / right;

        if (!std::isfinite(value)) {
            m_evaluatedExpression = prettyExpression(m_stack);
            m_errored = true;
            return;
        }

        m_stack << QString::number(value, 'g', 17);
        m_resultValue = value;
        m_result = formatNumber(value);
        m_justEvaluated = true;
        return;
    }

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
    if (m_rpnMode) {
        if (m_errored)
            return;
        if (!m_entry.isEmpty()) {
            pushEntry();
        } else if (!m_stack.isEmpty()) {
            // Empty Enter duplicates the top of the stack, matching the usual
            // RPN "x ENT ×" square.
            m_stack << m_stack.last();
        }
        m_justEvaluated = false;
        return;
    }

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

    if (m_rpnMode) {
        if (m_justEvaluated)
            m_justEvaluated = false;
        if (!m_entry.isEmpty()) {
            bool ok = false;
            const double value = QLocale::c().toDouble(sealNumber(m_entry), &ok);
            if (ok)
                m_entry = formatNumber(value / 100.0);
            return;
        }
        if (!m_stack.isEmpty()) {
            bool ok = false;
            const double value = QLocale::c().toDouble(m_stack.last(), &ok);
            if (ok)
                m_stack.last() = QString::number(value / 100.0, 'g', 17);
        }
        return;
    }

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

    if (m_rpnMode) {
        if (m_justEvaluated)
            m_justEvaluated = false;
        if (!m_entry.isEmpty()) {
            if (m_entry.startsWith(QLatin1Char('-')))
                m_entry.remove(0, 1);
            else
                m_entry.prepend(QLatin1Char('-'));
            return;
        }
        if (!m_stack.isEmpty()) {
            bool ok = false;
            const double value = QLocale::c().toDouble(m_stack.last(), &ok);
            if (ok)
                m_stack.last() = QString::number(-value, 'g', 17);
            return;
        }
        m_entry = QStringLiteral("-0");
        return;
    }

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

    if (m_rpnMode) {
        if (!m_entry.isEmpty()) {
            m_entry.chop(1);
            if (m_entry == QStringLiteral("-"))
                m_entry.clear();
            return;
        }
        if (!m_stack.isEmpty())
            m_stack.removeLast();
        m_justEvaluated = false;
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
    m_stack.clear();
    m_entry.clear();
    clearEvaluation();
    m_errored = false;
}

void Backend::pushEntry() {
    if (m_entry.isEmpty())
        return;
    m_stack << sealNumber(m_entry);
    m_entry.clear();
    m_justEvaluated = false;
}

void Backend::pressSwap() {
    if (!m_rpnMode || m_errored)
        return;

    pushEntry();
    if (m_stack.size() < 2)
        return;

    const int last = m_stack.size() - 1;
    m_stack.swapItemsAt(last - 1, last);
    m_justEvaluated = false;
}

void Backend::toggleRpnMode() {
    setRpnMode(!m_rpnMode);
}

void Backend::setRpnMode(bool rpnMode) {
    if (m_rpnMode == rpnMode)
        return;

    QString seed;
    if (!m_errored) {
        if (m_rpnMode) {
            if (!m_entry.isEmpty())
                seed = sealNumber(m_entry);
            else if (!m_stack.isEmpty())
                seed = m_stack.last();
        } else if (m_justEvaluated) {
            seed = QString::number(m_resultValue, 'g', 17);
        } else if (!m_entry.isEmpty()) {
            seed = sealNumber(m_entry);
        } else if (!m_tokens.isEmpty()) {
            seed = currentValue();
        }
    }

    m_rpnMode = rpnMode;
    pressClear();
    if (!seed.isEmpty() && seed != QStringLiteral("0")) {
        if (m_rpnMode)
            m_stack << seed;
        else
            m_entry = formatNumber(QLocale::c().toDouble(seed));
    }

    QSettings().setValue(QStringLiteral("calculator/rpnMode"), m_rpnMode);
    emit rpnModeChanged();
    emit calculationChanged();
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

    // First fold × and ÷ into their neighbors, then sum what remains, giving
    // multiplication its usual precedence over addition.
    QList<double> values;
    QStringList additiveOperators;

    bool numberOk = false;
    values << QLocale::c().toDouble(tokens.first(), &numberOk);
    if (!numberOk)
        return 0;

    for (int i = 1; i + 1 < tokens.size(); i += 2) {
        const QString &op = tokens.at(i);
        const double operand = QLocale::c().toDouble(tokens.at(i + 1), &numberOk);
        if (!numberOk || !isOperator(op))
            return 0;

        if (op == multiplySign) {
            values.last() *= operand;
        } else if (op == divideSign) {
            values.last() /= operand;
        } else {
            additiveOperators << op;
            values << operand;
        }
    }

    double total = values.first();
    for (int i = 0; i < additiveOperators.size(); ++i) {
        if (additiveOperators.at(i) == plusSign)
            total += values.at(i + 1);
        else
            total -= values.at(i + 1);
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

    if (m_errored)
        pressClear();
    else if (m_justEvaluated && !m_rpnMode)
        pressClear();
    else if (m_justEvaluated)
        m_justEvaluated = false;
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
