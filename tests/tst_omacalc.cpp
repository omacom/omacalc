#include <QtTest>
#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QRect>
#include <QSettings>
#include <QVariant>
#include <cmath>

#include "backend.h"
#include "systemtheme.h"

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

        // Binary-float noise stays out of the display.
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
        QCOMPARE(calculator.display(), Backend::formatNumber(std::pow(2.0, 0.1)));

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
        QCOMPARE(calculator.display(), QStringLiteral("123456789123456"));

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
    }

    void evaluatesTokens() {
        bool ok = false;
        QCOMPARE(Backend::evaluateTokens({"2", "+", "3", "×", "4"}, &ok), 14.0);
        QVERIFY(ok);

        QCOMPARE(Backend::evaluateTokens({"2", "^", "3"}, &ok), 8.0);
        QVERIFY(ok);

        QCOMPARE(Backend::evaluateTokens({"2", "^", "3", "^", "2"}, &ok), 512.0);
        QVERIFY(ok);

        Backend::evaluateTokens({"2", "+"}, &ok);
        QVERIFY(!ok);

        Backend::evaluateTokens({"1", "÷", "0"}, &ok);
        QVERIFY(!ok);
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

    void groupsThousandsOnTheDisplay() {
        Backend calculator;
        press(calculator, "5 0 0 * 4 =");
        QCOMPARE(calculator.display(), QStringLiteral("2,000"));
        QCOMPARE(calculator.expression(), QStringLiteral("500 × 4"));
        calculator.copyResult();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("2000"));
    }

    void formatsNumbers() {
        QCOMPARE(Backend::formatNumber(133), QStringLiteral("133"));
        QCOMPARE(Backend::formatNumber(0.1 + 0.2), QStringLiteral("0.3"));
        QCOMPARE(Backend::formatNumber(-0.0), QStringLiteral("0"));
        QCOMPARE(Backend::formatNumber(1e15), QStringLiteral("1e+15"));
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
#include "tst_omacalc.moc"
