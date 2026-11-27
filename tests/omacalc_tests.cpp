#include <QtTest>
#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QSettings>
#include <QVariant>
#include <cmath>

#include "backend.h"
#include "systemtheme.h"
#include "translator.h"

namespace {
void press(Backend &calculator, const QString &keys) {
    const QStringList sequence = keys.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &key : sequence)
        calculator.pressKey(key);
}
}

class OmacalcTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void startsAtZero() {
        Backend calculator;
        QCOMPARE(calculator.display(), QStringLiteral("0"));
        QCOMPARE(calculator.expression(), QString());
    }

    void calculatesPowers() {
        Backend calculator;
        press(calculator, "2 ^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("4"));

        press(calculator, "clear 2 × 3 ^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("18"));

        press(calculator, "clear 2 ^ 3 × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("16"));

        press(calculator, "clear 2 ^ 3 ^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("512"));

        press(calculator, "clear 2 ^ 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));

        press(calculator, "clear 2 ^ 3 = ^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("64"));

        press(calculator, "clear 2 sign ^ 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("-8"));

        press(calculator, "clear 0 ^ 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));

        press(calculator, "clear 0 ^ 1 sign =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));

        press(calculator, "clear 2 sign ^ 0 . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));
        press(calculator, "5");
        QCOMPARE(calculator.display(), QStringLiteral("5"));
    }

    void calculatesWithPrecedence() {
        Backend calculator;
        press(calculator, "4 2 × 3 + 7 =");
        QCOMPARE(calculator.display(), QStringLiteral("133"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 × 3 + 7"));

        press(calculator, "clear 2 + 3 × 4 =");
        QCOMPARE(calculator.display(), QStringLiteral("14"));

        press(calculator, "clear 1 0 - 4 ÷ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("8"));
    }

    void showsEntryWhileTyping() {
        Backend calculator;
        press(calculator, "4 2 ×");
        QCOMPARE(calculator.display(), QStringLiteral("42"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 ×"));

        press(calculator, "3");
        QCOMPARE(calculator.display(), QStringLiteral("3"));
    }

    void handlesDecimals() {
        Backend calculator;
        press(calculator, ". 5 + . 2 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.75"));

        // Decimals are held exactly, so there is no binary-float noise.
        press(calculator, "clear 0 . 1 + 0 . 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.3"));

        // A second decimal point in one number is ignored.
        press(calculator, "clear 1 . 5 . 5");
        QCOMPARE(calculator.display(), QStringLiteral("1.55"));
    }

    void divisionByZeroErrors() {
        Backend calculator;
        press(calculator, "1 ÷ 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));

        // Digits recover from an error without an explicit clear.
        press(calculator, "5");
        QCOMPARE(calculator.display(), QStringLiteral("5"));
    }

    void percentOfRunningTotal() {
        Backend calculator;

        // With a pending + or −, x% means x percent of the running total.
        press(calculator, "2 0 0 + 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("220"));

        press(calculator, "clear 2 0 0 - 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("180"));

        // With ×, ÷, or ^, or standalone, x% is simply x ÷ 100.
        press(calculator, "clear 2 0 0 × 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("20"));

        press(calculator, "clear 2 ^ 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("1.07177346253629"));

        press(calculator, "clear 5 0 %");
        QCOMPARE(calculator.display(), QStringLiteral("0.5"));

        // After equals, percent picks up from the result.
        press(calculator, "clear 4 0 + 1 0 = %");
        QCOMPARE(calculator.display(), QStringLiteral("0.5"));
    }

    void percentAndSign() {
        Backend calculator;
        press(calculator, "8 sign");
        QCOMPARE(calculator.display(), QStringLiteral("-8"));
        press(calculator, "sign");
        QCOMPARE(calculator.display(), QStringLiteral("8"));

        press(calculator, "clear 4 + 8 sign =");
        QCOMPARE(calculator.display(), QStringLiteral("-4"));
    }

    void signStartsNewOperand() {
        Backend calculator;

        // Sign with nothing typed starts a fresh negative operand rather than
        // negating the previous one: 4 + ± 2 = is 4 + (-2), not 4 + (-42).
        press(calculator, "4 + sign 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("2"));
        QCOMPARE(calculator.expression(), QStringLiteral("4 + -2"));

        press(calculator, "clear sign");
        QCOMPARE(calculator.display(), QStringLiteral("-0"));
        press(calculator, "5");
        QCOMPARE(calculator.display(), QStringLiteral("-5"));
    }

    void chainsWithFullPrecision() {
        Backend calculator;

        // Chaining continues from the exact value, not the rounded display.
        press(calculator, "1 ÷ 3 = × 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));

        // Integers within the 15-digit entry limit survive exactly.
        press(calculator, "clear 9 9 9 9 9 9 9 9 9 9 9 9 9 9 =");
        QCOMPARE(calculator.display(), QStringLiteral("99,999,999,999,999"));
    }

    void capsEntryAtFifteenDigits() {
        Backend calculator;
        press(calculator, "1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8");
        QCOMPARE(calculator.display(), QStringLiteral("123,456,789,123,456"));

        // The decimal point does not count against the digit cap.
        press(calculator, "clear . 1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7");
        QCOMPARE(calculator.display(), QStringLiteral("0.123456789123456"));
    }

    void backspaceEdits() {
        Backend calculator;
        press(calculator, "1 2 3 backspace");
        QCOMPARE(calculator.display(), QStringLiteral("12"));

        press(calculator, "backspace backspace backspace");
        QCOMPARE(calculator.display(), QStringLiteral("0"));
    }

    void treatsExponentFormAsAWholeValue() {
        Backend calculator;

        // A digit after a tiny percent starts a new number rather than
        // extending the exponent of 1e-05 into 1e-055.
        press(calculator, ". 0 0 1 % 5");
        QCOMPARE(calculator.display(), QStringLiteral("5"));

        press(calculator, "clear . 0 0 1 % .");
        QCOMPARE(calculator.display(), QStringLiteral("0."));

        // Backspace on 1e+15 clears it rather than leaving 1e+1, which is 10.
        press(calculator, "clear 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 × 1 0 = backspace");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        press(calculator, "clear . 0 0 1 % sign × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("-2e-05"));

        // A fresh number replaces only the operand, not the pending sum, and
        // a decimal point starts one too rather than landing after 1e-05.
        press(calculator, "clear 2 + . 0 0 1 % 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("7"));
        press(calculator, "clear 2 + . 0 0 1 % . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("2.5"));

        // Backspace leaves a zero operand, not the 2 before it.
        press(calculator, "clear 2 + . 0 0 1 % backspace");
        QCOMPARE(calculator.display(), QStringLiteral("0"));
        QCOMPARE(calculator.expression(), QStringLiteral("2 +"));
    }

    void percentOfAHugeTotal() {
        Backend calculator;

        // 1e9999 is the largest power of ten the display holds; taking 100 %
        // of it must not overflow on the way there.
        press(calculator, "1 0 ^ 9 9 9 9 = - 1 0 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        // The percentage itself must fit; the product on the way need not.
        press(calculator, "clear 1 0 ^ 9 9 9 9 = + 5 0 %");
        QCOMPARE(calculator.display(), QStringLiteral("5e+9998"));

        // A percentage beyond the display's range is an error.
        press(calculator, "clear 1 0 ^ 9 9 9 9 = + 1 0 0 0 %");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));
        QCOMPARE(calculator.expression(), QStringLiteral("1e+9999 + 1,000 %"));

        // So is one that is too small, even straight after a result.
        press(calculator, "clear 1 0 ^ 9 9 9 9 sign = %");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));
        QCOMPARE(calculator.expression(), QStringLiteral("1e-9999 %"));
    }

    void chainsFromResult() {
        Backend calculator;
        press(calculator, "6 × 7 = × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("84"));
        QCOMPARE(calculator.expression(), QStringLiteral("42 × 2"));

        // A digit after equals starts fresh instead of appending to the result.
        press(calculator, "9");
        QCOMPARE(calculator.display(), QStringLiteral("9"));
        QCOMPARE(calculator.expression(), QString());
    }

    void replacesDanglingOperator() {
        Backend calculator;
        press(calculator, "4 + × 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("8"));

        // Equals with a trailing operator drops it.
        press(calculator, "clear 9 + =");
        QCOMPARE(calculator.display(), QStringLiteral("9"));
    }

    void pastesNumbers() {
        Backend calculator;
        QClipboard *clipboard = QGuiApplication::clipboard();

        clipboard->setText(QStringLiteral(" 42.5 "));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("42.5"));

        // A pasted number is a normal entry that calculates like any other.
        press(calculator, "+ . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("43"));

        // Decimal commas are welcome; garbage is ignored.
        clipboard->setText(QStringLiteral("1,5"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1.5"));

        clipboard->setText(QStringLiteral("not a number"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1.5"));

        // Thousands separators, as the display shows them, paste back in.
        clipboard->setText(QStringLiteral("1,234,567.89"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1,234,567.89"));

        clipboard->setText(QStringLiteral("−12,000"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("-12,000"));
    }

    void groupsThousands() {
        Backend calculator;
        press(calculator, "1 2 3 4");
        QCOMPARE(calculator.display(), QStringLiteral("1,234"));

        // The fraction is not grouped, and a trailing point stays visible.
        press(calculator, ". 5 6 7 8");
        QCOMPARE(calculator.display(), QStringLiteral("1,234.5678"));

        press(calculator, "× 1 0 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("1,234,567.8"));
        QCOMPARE(calculator.expression(), QStringLiteral("1,234.5678 × 1,000"));

        // Chaining and editing after a result work from the plain value.
        press(calculator, "+ 1 =");
        QCOMPARE(calculator.display(), QStringLiteral("1,234,568.8"));
        press(calculator, "backspace backspace");
        QCOMPARE(calculator.display(), QStringLiteral("1,234,568"));

        press(calculator, "clear 1 0 0 0 0 0 0 sign");
        QCOMPARE(calculator.display(), QStringLiteral("-1,000,000"));
    }

    void copiesWithoutSeparators() {
        Backend calculator;
        press(calculator, "1 2 3 4 5 6 7 . 5");
        calculator.copyResult();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("1234567.5"));
    }

    void groupsDigits() {
        QCOMPARE(Backend::groupDigits(QStringLiteral("0")), QStringLiteral("0"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("999")), QStringLiteral("999"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("1000")), QStringLiteral("1,000"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("-100000")), QStringLiteral("-100,000"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("0.123456")), QStringLiteral("0.123456"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("1.5e+20")), QStringLiteral("1.5e+20"));
        QCOMPARE(Backend::groupDigits(QStringLiteral("Error")), QStringLiteral("Error"));
    }

    void evaluatesTokens() {
        const auto evaluate = [](const QStringList &pieces) -> QString {
            QList<Backend::Token> tokens;
            for (const QString &piece : pieces) {
                const std::optional<Number> number = Number::parse(piece);
                tokens << (number ? Backend::Token::value(*number) : Backend::Token::operation(piece));
            }
            const std::optional<Number> result = Backend::evaluate(tokens);
            return result ? result->toString() : QStringLiteral("Error");
        };

        QCOMPARE(evaluate({"2", "+", "3", "×", "4"}), QStringLiteral("14"));
        QCOMPARE(evaluate({"2", "^", "3"}), QStringLiteral("8"));
        QCOMPARE(evaluate({"2", "^", "3", "^", "2"}), QStringLiteral("512"));
        QCOMPARE(evaluate({"2", "+"}), QStringLiteral("Error"));
        QCOMPARE(evaluate({"1", "÷", "0"}), QStringLiteral("Error"));
        QCOMPARE(evaluate({"2", "2"}), QStringLiteral("Error"));
    }

    void opensAtTheDesignSizeForTheTextScale() {
        Backend calculator;
        calculator.setTextScale(1);
        QVariantMap size = calculator.defaultWindowSize();
        QCOMPARE(size.value(QStringLiteral("width")).toInt(), 400);
        QCOMPARE(size.value(QStringLiteral("height")).toInt(), 568);

        calculator.setTextScale(1.5);
        size = calculator.defaultWindowSize();
        QCOMPARE(size.value(QStringLiteral("width")).toInt(), 600);
        QCOMPARE(size.value(QStringLiteral("height")).toInt(), 852);

        // Below the design size the window stays at Main.qml's minimum.
        calculator.setTextScale(0.5);
        size = calculator.defaultWindowSize();
        QCOMPARE(size.value(QStringLiteral("width")).toInt(), 300);
        QCOMPARE(size.value(QStringLiteral("height")).toInt(), 430);

        calculator.setTextScale(qQNaN());
        size = calculator.defaultWindowSize();
        QCOMPARE(size.value(QStringLiteral("width")).toInt(), 400);
        QCOMPARE(size.value(QStringLiteral("height")).toInt(), 568);
    }

    void dropsASavedWindowRectangle() {
        QSettings written;
        written.setValue(QStringLiteral("window/geometry"), QRect(0, 0, 1896, 1030));
        written.setValue(QStringLiteral("window/maximized"), true);
        written.setValue(QStringLiteral("unrelated"), QStringLiteral("kept"));
        written.sync();

        Backend calculator;
        Q_UNUSED(calculator);

        QSettings stored;
        QVERIFY(!stored.contains(QStringLiteral("window/geometry")));
        QVERIFY(!stored.contains(QStringLiteral("window/maximized")));
        QCOMPARE(stored.value(QStringLiteral("unrelated")).toString(), QStringLiteral("kept"));
    }

    void normalizesThePortalTextScale() {
        bool known = false;
        QCOMPARE(SystemTheme::normalizedTextScale(QVariant(1.25), &known), 1.25);
        QVERIFY(known);

        known = false;
        QCOMPARE(SystemTheme::normalizedTextScale(QVariant(4.0), &known), 3.0);
        QVERIFY(known);

        known = false;
        QCOMPARE(SystemTheme::normalizedTextScale(QVariant(0.1), &known), 0.5);
        QVERIFY(known);

        known = false;
        QCOMPARE(SystemTheme::normalizedTextScale(QVariant(0.0), &known), 1.0);
        QVERIFY(!known);

        known = false;
        QCOMPARE(SystemTheme::normalizedTextScale(QVariant(QStringLiteral("nope")), &known), 1.0);
        QVERIFY(!known);
    }

    void readsThePortalTextScaleWithoutStalling() {
        QElapsedTimer timer;
        timer.start();
        SystemTheme theme;
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(theme.textScale() >= 0.5);
        QVERIFY(theme.textScale() <= 3.0);
    }

    void everyCatalogParses() {
        // A broken catalog loads as nothing, which shows English rather than failing.
        const QString catalogs = QFINDTESTDATA("../src/i18n");
        QVERIFY(!catalogs.isEmpty());
        const QStringList files = QDir(catalogs).entryList({QStringLiteral("*.json")});
        QVERIFY(!files.isEmpty());
        for (const QString &name : files) {
            QFile file(catalogs + QLatin1Char('/') + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonDocument catalog = QJsonDocument::fromJson(file.readAll(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError && catalog.isObject(),
                     qPrintable(name + QStringLiteral(": ") + error.errorString()));
            for (const QJsonValue &value : catalog.object())
                QVERIFY2(value.isString() && !value.toString().isEmpty(), qPrintable(name));
        }
    }

    void showsErrorInTheOmarchyLanguage() {
        QTemporaryDir directory;
        QFile language(directory.filePath(QStringLiteral("language")));
        QVERIFY(language.open(QIODevice::WriteOnly));
        language.write("da\n");
        language.close();

        Translator translator(language.fileName(), QFINDTESTDATA("../src/i18n"));
        QCoreApplication::installTranslator(&translator);
        Backend calculator;
        press(calculator, "1 ÷ 0 =");
        const QString display = calculator.display();
        QCoreApplication::removeTranslator(&translator);
        QGuiApplication::setLayoutDirection(Qt::LeftToRight);
        QLocale::setDefault(QLocale::c());

        QCOMPARE(display, QStringLiteral("Fejl"));
    }

    void formatsNumbers() {
        const auto format = [](const char *text) {
            const std::optional<Number> number = Number::parse(QString::fromLatin1(text));
            return number ? number->toString() : QStringLiteral("refused");
        };

        QCOMPARE(format("133"), QStringLiteral("133"));
        QCOMPARE(format("-0"), QStringLiteral("0"));
        QCOMPARE(format("1e15"), QStringLiteral("1e+15"));
        QCOMPARE(format("123456789012345"), QStringLiteral("123456789012345"));
        QCOMPARE(format("0.0001"), QStringLiteral("0.0001"));
        QCOMPARE(format("0.00001"), QStringLiteral("1e-05"));
        QCOMPARE(format("-1.50e+20"), QStringLiteral("-1.5e+20"));
        QCOMPARE(format(".5"), QStringLiteral("0.5"));
        QCOMPARE(format("5."), QStringLiteral("5"));
        QCOMPARE(format("+7"), QStringLiteral("7"));

        // Sixteen digits round to fifteen, half away from zero, carrying
        // into a new leading digit when they must.
        QCOMPARE(format("1.234567890123455"), QStringLiteral("1.23456789012346"));
        QCOMPARE(format("-1.234567890123455"), QStringLiteral("-1.23456789012346"));
        QCOMPARE(format("1.234567890123454999"), QStringLiteral("1.23456789012345"));
        QCOMPARE(format("9.999999999999995"), QStringLiteral("10"));
        QCOMPARE(format("999999999999999.5"), QStringLiteral("1e+15"));

        // The display's range ends at exponents of ±9999.
        QCOMPARE(format("9.99999999999999e9999"), QStringLiteral("9.99999999999999e+9999"));
        QCOMPARE(format("1e-9999"), QStringLiteral("1e-9999"));
        QCOMPARE(format("9.999999999999995e9999"), QStringLiteral("refused"));
        QCOMPARE(format("1e10000"), QStringLiteral("refused"));
        QCOMPARE(format("9e-10000"), QStringLiteral("refused"));
        QCOMPARE(format("9.999999999999995e-10000"), QStringLiteral("1e-9999"));
        QCOMPARE(format("1e999999999"), QStringLiteral("refused"));
        QCOMPARE(format("0e999999999"), QStringLiteral("0"));

        QCOMPARE(format(""), QStringLiteral("refused"));
        QCOMPARE(format("."), QStringLiteral("refused"));
        QCOMPARE(format("-"), QStringLiteral("refused"));
        QCOMPARE(format("1e"), QStringLiteral("refused"));
        QCOMPARE(format("inf"), QStringLiteral("refused"));
        QCOMPARE(format("nan"), QStringLiteral("refused"));
        QCOMPARE(format("0x10"), QStringLiteral("refused"));
        QCOMPARE(format("1.2.3"), QStringLiteral("refused"));
    }

    // The calculations that used to show binary-float noise.
    void calculatesExactly() {
        Backend calculator;
        press(calculator, "1 3 3 7 3 . 5 4 - 1 3 4 3 8 . 7 7 =");
        QCOMPARE(calculator.display(), QStringLiteral("-65.23"));

        press(calculator, "clear 0 . 1 + 0 . 2 - 0 . 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        press(calculator, "clear 1 0 0 0 0 0 0 . 1 - 1 0 0 0 0 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.1"));

        press(calculator, "clear 1 . 1 5 × 1 0 0 - 1 1 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        press(calculator, "clear 1 ÷ 3 × 3 - 1 =");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        press(calculator, "clear 1 ÷ 7 = × 7 - 1 =");
        QCOMPARE(calculator.display(), QStringLiteral("0"));

        press(calculator, "clear 1 ÷ 3 + 1 0 % =");
        QCOMPARE(calculator.display(), QStringLiteral("0.366666666666667"));

        // Ties round away from zero, the way a calculator is expected to,
        // where binary rounding would have kept 100,000,000,000,000.
        press(calculator, "clear 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 + . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("100,000,000,000,001"));
    }

    void findsExactRoots() {
        Backend calculator;
        const QList<QPair<const char *, const char *>> roots = {
            {"4 ^ . 5 - 2 =", "0"},
            {"0 . 2 5 ^ . 5 =", "0.5"},
            {"1 . 2 1 ^ . 5 =", "1.1"},
            {"1 . 2 1 ^ . 5 - 1 . 1 =", "0"},
            {"3 2 ^ . 2 =", "2"},
            {"1 6 ^ . 7 5 =", "8"},
            {"4 ^ . 5 sign =", "0.5"},
            {"4 ^ 1 . 5 =", "8"},
            {"0 ^ . 5 =", "0"},
            {"1 ^ . 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 =", "1"},
            {"1 0 0 0 0 0 0 ^ . 5 =", "1,000"},
        };
        for (const auto &[keys, expected] : roots) {
            press(calculator, QStringLiteral("clear ") + QString::fromLatin1(keys));
            QCOMPARE(calculator.display(), QString::fromLatin1(expected));
        }
    }

    void approximatesIrrationalRoots() {
        Backend calculator;
        press(calculator, "2 ^ . 5 =");
        QCOMPARE(calculator.display(), QStringLiteral("1.4142135623731"));

        press(calculator, "^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("2"));

        press(calculator, "clear 2 ^ . 5 ^ 2 =");
        QCOMPARE(calculator.display(), QStringLiteral("1.18920711500272"));

        // An irrational root is held to about 77 digits, so its leftover
        // after cancelling is that small, and never shows as a wrong digit.
        press(calculator, "clear 2 ^ . 5 × 2 ^ . 5 - 2 =");
        const std::optional<Number> leftover = calculator.value();
        QVERIFY(leftover);
        QVERIFY(abs(leftover->fraction()) < mpq_class(1, mpz_class("1" + std::string(70, '0'))));
    }

    void approximatesHugePowers() {
        Backend calculator;

        // Far too long to hold exactly, so carried to about 77 digits.
        press(calculator, "1 . 0 0 0 1 ^ 1 0 0 0 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("22,015.4560485522"));

        press(calculator, "clear 1 + 0 . 0 5 ÷ 3 6 5 = ^ 1 0 9 5 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("4.48122868852452"));

        // (1 + 1/n)^n approaches e; binary floats lose it entirely.
        press(calculator, "clear 1 + 0 . 0 0 0 0 0 0 0 0 0 0 0 0 0 1 = ^ 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("2.71828182845903"));

        press(calculator, "clear 3 ^ 2 0 0 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("2.66130342721742e+9542"));

        // A negative base keeps its sign through an odd power, exact or not.
        press(calculator, "clear 1 sign ^ 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("-1"));
        press(calculator, "clear 1 sign ^ 1 2 3 4 5 6 7 8 9 0 1 2 3 4 4 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));
        press(calculator, "clear 1 . 0 0 0 1 sign ^ 1 0 0 0 0 1 =");
        QCOMPARE(calculator.display(), QStringLiteral("-22,017.6575941571"));
        press(calculator, "clear 1 . 1 5 sign ^ 9 9 9 9 9 =");
        QCOMPARE(calculator.display(), QStringLiteral("-5.28856103411283e+6069"));
    }

    void staysWithinTheDisplayRange() {
        Backend calculator;
        press(calculator, "1 0 ^ 9 9 9 9 =");
        QCOMPARE(calculator.display(), QStringLiteral("1e+9999"));
        press(calculator, "× 1 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));

        press(calculator, "clear 1 0 ^ 9 9 9 9 sign =");
        QCOMPARE(calculator.display(), QStringLiteral("1e-9999"));
        press(calculator, "÷ 1 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));

        // Every number along the way must fit, as with a hardware calculator.
        press(calculator, "clear 1 0 ^ 9 9 9 9 = × 1 0 ÷ 1 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("Error"));
    }

    void refusesHugePowersQuickly() {
        Backend calculator;
        QElapsedTimer timer;
        timer.start();
        const QStringList hopeless = {
            "2 ^ 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 =",
            "0 . 5 ^ 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 =",
            "1 . 0 0 0 0 0 0 1 ^ 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 =",
            "2 ^ . 5 ^ 9 9 9 9 9 9 9 9 9 9 9 9 9 9 9 =",
            "9 ^ 9 ^ 9 =",
        };
        for (const QString &keys : hopeless) {
            press(calculator, QStringLiteral("clear ") + keys);
            QCOMPARE(calculator.display(), QStringLiteral("Error"));
        }
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed()) + " ms"));
    }

    // Starting an edit from a result, a percent or a paste keeps every digit
    // until a digit is changed.
    void keepsExactValuesWhileEditing() {
        Backend calculator;
        press(calculator, "1 ÷ 3 = sign × 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("-1"));

        press(calculator, "clear 1 ÷ 3 = % × 3 0 0 =");
        QCOMPARE(calculator.display(), QStringLiteral("1"));

        press(calculator, "clear 2 ÷ 3 = .");
        QCOMPARE(calculator.display(), QStringLiteral("0."));

        // Backspace edits the digits shown, which then are the number.
        press(calculator, "clear 1 ÷ 3 = backspace × 3 =");
        QCOMPARE(calculator.display(), QStringLiteral("0.99999999999999"));

        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(QStringLiteral("0.1000000000000000001"));
        press(calculator, "clear");
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("0.1"));
        press(calculator, "- . 1 =");
        QCOMPARE(calculator.display(), QStringLiteral("1e-19"));

        clipboard->setText(QStringLiteral("1e10000"));
        calculator.pasteNumber();
        QCOMPARE(calculator.display(), QStringLiteral("1e-19"));
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\" # a light theme\n"
            "accent = \"#112233\" # button accent\n"
            "selection = \"#445566\" # selection color\n"
            "background = \"#fefefe\" # canvas\n"
            "foreground = \"#101010\" # text\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend calculator;
        QCOMPARE(calculator.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(calculator.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(calculator.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(calculator.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!calculator.darkMode());

        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        const QByteArray invalidPalette(
            "mode = \"light\"\n"
            "background = \"not a color\" # invalid canvas\n"
            "foreground = #101010 # bare hex text\n");
        QCOMPARE(colorsFile.write(invalidPalette), qint64(invalidPalette.size()));
        colorsFile.close();

        Backend withInvalidColor;
        QCOMPARE(withInvalidColor.themeBackground(), QStringLiteral("#ffffff"));
        QCOMPARE(withInvalidColor.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(withInvalidColor.themeAccent(), QStringLiteral("#2077b2"));
        QCOMPARE(withInvalidColor.themeSelection(), QStringLiteral("#2077b2"));
        QVERIFY(!withInvalidColor.darkMode());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmacalcTest)
#include "omacalc_tests.moc"
