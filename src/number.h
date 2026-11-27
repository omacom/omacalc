#pragma once

#include <QString>
#include <gmpxx.h>
#include <optional>

// A number the calculator holds: an exact fraction of two whole numbers, so
// 0.1, 13373.54 and 1 ÷ 3 are each held exactly, and + − × ÷ never round.
// 0.1 + 0.2 − 0.3 is exactly 0, and 1 ÷ 3 × 3 is exactly 1.
//
// Only two kinds of result have no fraction small enough to hold, and only
// those are rounded, to kApproximationBits (about 77 significant digits):
//   - a root whose true value is irrational, like 2 ^ 0.5. A root with an
//     exact value, like 4 ^ 0.5 or 0.25 ^ 0.5, is found and kept exact.
//   - a fraction whose numerator or denominator outgrows kMaxExactBits, like
//     a compound-interest power 1.0001 ^ 100000.
//
// Every operation returns a number whose displayed form stays within
// ±kMaxDecimalExponent, or nothing, which the calculator shows as Error:
// dividing by zero, a non-whole power of a negative number, or a result too
// large or too small for the display.
class Number {
public:
    static constexpr int kDisplayDigits = 15;
    static constexpr long kMaxDecimalExponent = 9999;
    static constexpr unsigned long kMaxExactBits = 65536;
    static constexpr unsigned long kApproximationBits = 256;

    Number() = default;
    explicit Number(long whole);

    // Plain decimal text: an optional sign, digits with an optional point,
    // and an optional exponent, as in "-13373.54", ".5", "5." or "2e-05".
    static std::optional<Number> parse(const QString &text);

    // Fifteen significant digits, rounded half away from zero, laid out like
    // printf's %g: "0.3", "-65.23", "123456789012345", "1e+15", "2e-05".
    QString toString() const;

    bool isZero() const { return sgn(m_value) == 0; }
    const mpq_class &fraction() const { return m_value; }
    Number negated() const;

    static std::optional<Number> add(const Number &a, const Number &b);
    static std::optional<Number> subtract(const Number &a, const Number &b);
    static std::optional<Number> multiply(const Number &a, const Number &b);
    static std::optional<Number> divide(const Number &a, const Number &b);
    static std::optional<Number> power(const Number &base, const Number &exponent);
    // value ÷ 100, or with a running total, total × value ÷ 100 in one step,
    // so only the percentage itself has to fit the display's range.
    static std::optional<Number> percent(const Number &value);
    static std::optional<Number> percent(const Number &value, const Number &total);

    friend bool operator==(const Number &a, const Number &b) { return a.m_value == b.m_value; }
    friend bool operator!=(const Number &a, const Number &b) { return !(a == b); }

private:
    static std::optional<Number> fromFraction(mpq_class value);
    static std::optional<Number> wholePower(const mpq_class &base, const mpz_class &exponent);
    static std::optional<Number> approximatePower(mpq_class base, const mpq_class &exponent);

    mpq_class m_value;
};
