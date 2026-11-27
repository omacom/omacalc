#include "number.h"

#include <QRegularExpression>
#include <algorithm>
#include <cstdlib>
#include <mpfr.h>

namespace {
// 10^9999 is just under 2^33217. A fraction whose numerator and denominator
// differ in length by more than this many bits is out of the display's range
// however it rounds, so it is refused before any digit work.
constexpr long kRangeBits = 33300;

// An MPFR float that frees itself.
struct Float {
    mpfr_t value;
    explicit Float(mpfr_prec_t precision) { mpfr_init2(value, precision); }
    ~Float() { mpfr_clear(value); }
    Float(const Float &) = delete;
    Float &operator=(const Float &) = delete;
};

unsigned long bitLength(const mpz_class &value) {
    return mpz_sizeinbase(value.get_mpz_t(), 2);
}

mpz_class powerOfTen(unsigned long exponent) {
    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), 10, exponent);
    return result;
}

// |value| rounded to `count` significant digits, half away from zero:
// significand × 10^(exponent − count + 1), with exactly `count` digits in the
// significand. value must not be zero.
struct Rounded {
    mpz_class significand;
    long exponent;
};

Rounded roundToDigits(const mpq_class &value, int count) {
    const mpz_class numerator = abs(value.get_num());
    const mpz_class &denominator = value.get_den();
    const mpz_class smallest = powerOfTen(count - 1);
    const mpz_class limit = powerOfTen(count);

    // The difference in decimal lengths is within two of the true exponent;
    // the loop settles it exactly.
    long exponent = long(mpz_sizeinbase(numerator.get_mpz_t(), 10))
        - long(mpz_sizeinbase(denominator.get_mpz_t(), 10));
    for (;;) {
        const long shift = count - 1 - exponent;
        mpz_class scaledNumerator = numerator;
        mpz_class scaledDenominator = denominator;
        if (shift >= 0)
            scaledNumerator *= powerOfTen(shift);
        else
            scaledDenominator *= powerOfTen(-shift);

        mpz_class quotient;
        mpz_class remainder;
        mpz_fdiv_qr(quotient.get_mpz_t(), remainder.get_mpz_t(),
                    scaledNumerator.get_mpz_t(), scaledDenominator.get_mpz_t());
        if (quotient < smallest) {
            --exponent;
            continue;
        }
        if (quotient >= limit) {
            ++exponent;
            continue;
        }

        if (2 * remainder >= scaledDenominator)
            ++quotient;
        if (quotient == limit) {
            quotient = smallest;
            ++exponent;
        }
        return {quotient, exponent};
    }
}

// The q-th root of whole c ≥ 1, when that root is whole. A root of 2 or more
// needs c ≥ 2^q, so a q as long as c settles it without any root-taking.
bool perfectRoot(const mpz_class &c, const mpz_class &q, mpz_class &root) {
    if (c == 1) {
        root = 1;
        return true;
    }
    if (q >= bitLength(c))
        return false;
    return mpz_root(root.get_mpz_t(), c.get_mpz_t(), q.get_ui()) != 0;
}
}

Number::Number(long whole) : m_value(whole) {}

std::optional<Number> Number::parse(const QString &text) {
    static const QRegularExpression pattern(
        QStringLiteral("^([+-]?)([0-9]*)(?:\\.([0-9]*))?(?:[eE]([+-]?[0-9]{1,9}))?$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;

    const QString whole = match.captured(2);
    const QString fraction = match.captured(3);
    if (whole.isEmpty() && fraction.isEmpty())
        return std::nullopt;

    const mpz_class digits((whole + fraction).toStdString(), 10);
    if (digits == 0)
        return Number();

    // Settle the range from the digit count and exponent alone, so a pasted
    // 1e999999999 is refused without building a billion-digit power of ten.
    long exponent = match.captured(4).isEmpty() ? 0 : match.captured(4).toLong();
    exponent -= fraction.size();
    const long leading = exponent + long(mpz_sizeinbase(digits.get_mpz_t(), 10)) - 1;
    if (leading > kMaxDecimalExponent + 1 || leading < -kMaxDecimalExponent - 2)
        return std::nullopt;

    mpq_class value(digits);
    if (exponent >= 0)
        value *= powerOfTen(exponent);
    else
        value /= powerOfTen(-exponent);
    if (match.captured(1) == QStringLiteral("-"))
        value = -value;
    return fromFraction(value);
}

QString Number::toString() const {
    if (isZero())
        return QStringLiteral("0");

    const Rounded rounded = roundToDigits(m_value, kDisplayDigits);
    QString digits = QString::fromStdString(rounded.significand.get_str());
    while (digits.size() > 1 && digits.endsWith(QLatin1Char('0')))
        digits.chop(1);

    QString text;
    const long exponent = rounded.exponent;
    if (exponent < -4 || exponent >= kDisplayDigits) {
        text = digits.left(1);
        if (digits.size() > 1)
            text += QLatin1Char('.') + digits.mid(1);
        const long size = std::labs(exponent);
        text += exponent < 0 ? QStringLiteral("e-") : QStringLiteral("e+");
        if (size < 10)
            text += QLatin1Char('0');
        text += QString::number(size);
    } else if (exponent < 0) {
        text = QStringLiteral("0.") + QString(int(-exponent - 1), QLatin1Char('0')) + digits;
    } else {
        const int whole = int(exponent) + 1;
        if (digits.size() <= whole)
            text = digits + QString(whole - digits.size(), QLatin1Char('0'));
        else
            text = digits.left(whole) + QLatin1Char('.') + digits.mid(whole);
    }

    if (sgn(m_value) < 0)
        text.prepend(QLatin1Char('-'));
    return text;
}

Number Number::negated() const {
    Number result;
    result.m_value = -m_value;
    return result;
}

std::optional<Number> Number::add(const Number &a, const Number &b) {
    return fromFraction(a.m_value + b.m_value);
}

std::optional<Number> Number::subtract(const Number &a, const Number &b) {
    return fromFraction(a.m_value - b.m_value);
}

std::optional<Number> Number::multiply(const Number &a, const Number &b) {
    return fromFraction(a.m_value * b.m_value);
}

std::optional<Number> Number::divide(const Number &a, const Number &b) {
    if (b.isZero())
        return std::nullopt;
    return fromFraction(a.m_value / b.m_value);
}

std::optional<Number> Number::percent(const Number &value) {
    return fromFraction(value.m_value / 100);
}

std::optional<Number> Number::percent(const Number &value, const Number &total) {
    return fromFraction(total.m_value * value.m_value / 100);
}

std::optional<Number> Number::power(const Number &base, const Number &exponent) {
    const mpq_class &x = base.m_value;
    const mpq_class &y = exponent.m_value;
    if (y.get_den() == 1)
        return wholePower(x, y.get_num());

    // A non-whole power of a negative number has no real value.
    if (sgn(x) < 0)
        return std::nullopt;
    if (sgn(x) == 0)
        return sgn(y) > 0 ? std::optional<Number>(Number()) : std::nullopt;

    // With x = a/b and y = p/q in lowest terms, x^y is a fraction exactly
    // when a and b are both perfect q-th powers. That is checked, not
    // estimated, so 4 ^ 0.5 is exactly 2 and 2 ^ 0.5 is known irrational.
    mpz_class numeratorRoot;
    mpz_class denominatorRoot;
    if (perfectRoot(x.get_num(), y.get_den(), numeratorRoot)
            && perfectRoot(x.get_den(), y.get_den(), denominatorRoot)) {
        mpq_class root(numeratorRoot, denominatorRoot);
        root.canonicalize();
        return wholePower(root, y.get_num());
    }
    return approximatePower(x, y);
}

std::optional<Number> Number::fromFraction(mpq_class value) {
    if (sgn(value) == 0)
        return Number();

    const long magnitude = long(bitLength(value.get_num())) - long(bitLength(value.get_den()));
    if (magnitude > kRangeBits || magnitude < -kRangeBits)
        return std::nullopt;

    if (bitLength(value.get_num()) > kMaxExactBits || bitLength(value.get_den()) > kMaxExactBits) {
        Float rounded(kApproximationBits);
        mpfr_set_q(rounded.value, value.get_mpq_t(), MPFR_RNDN);
        mpfr_get_q(value.get_mpq_t(), rounded.value);
        value.canonicalize();
    }

    const long exponent = roundToDigits(value, kDisplayDigits).exponent;
    if (exponent > kMaxDecimalExponent || exponent < -kMaxDecimalExponent)
        return std::nullopt;

    Number number;
    number.m_value = std::move(value);
    return number;
}

std::optional<Number> Number::wholePower(const mpq_class &base, const mpz_class &exponent) {
    if (sgn(exponent) == 0)
        return Number(1);
    if (sgn(base) == 0)
        return sgn(exponent) > 0 ? std::optional<Number>(Number()) : std::nullopt;
    if (abs(base) == 1)
        return Number(base > 0 || mpz_even_p(exponent.get_mpz_t()) ? 1 : -1);

    // The exact power's numerator and denominator have at most
    // count × widest bits; past kMaxExactBits it would be rounded anyway.
    const mpz_class count = abs(exponent);
    const unsigned long widest = std::max(bitLength(base.get_num()), bitLength(base.get_den()));
    if (count.fits_ulong_p() && count.get_ui() <= kMaxExactBits / widest) {
        mpz_class numerator;
        mpz_class denominator;
        mpz_pow_ui(numerator.get_mpz_t(), base.get_num().get_mpz_t(), count.get_ui());
        mpz_pow_ui(denominator.get_mpz_t(), base.get_den().get_mpz_t(), count.get_ui());
        mpq_class result(numerator, denominator);
        if (sgn(exponent) < 0)
            result = 1 / result;
        return fromFraction(result);
    }
    return approximatePower(base, mpq_class(exponent));
}

std::optional<Number> Number::approximatePower(mpq_class base, const mpq_class &exponent) {
    // A negative base reaches here only with a whole exponent.
    bool negative = false;
    if (sgn(base) < 0) {
        negative = mpz_odd_p(exponent.get_num().get_mpz_t());
        base = -base;
    }

    // Rounding the base by a relative e changes base^y by about y × e, so a
    // large exponent needs that many more working bits for the result to
    // keep kApproximationBits.
    const long exponentBits = long(bitLength(exponent.get_num()))
        - long(bitLength(exponent.get_den()));
    const mpfr_prec_t precision = mpfr_prec_t(kApproximationBits) + 96
        + std::max(0L, exponentBits);
    Float x(precision);
    Float y(precision);
    Float result(precision);
    mpfr_set_q(x.value, base.get_mpq_t(), MPFR_RNDN);
    mpfr_set_q(y.value, exponent.get_mpq_t(), MPFR_RNDN);
    mpfr_pow(result.value, x.value, y.value, MPFR_RNDN);

    // Overflow, underflow, or so far out of range that building the
    // fraction would be wasted work.
    if (!mpfr_regular_p(result.value) || std::labs(mpfr_get_exp(result.value)) > kRangeBits)
        return std::nullopt;

    mpfr_prec_round(result.value, kApproximationBits, MPFR_RNDN);
    mpq_class value;
    mpfr_get_q(value.get_mpq_t(), result.value);
    value.canonicalize();
    if (negative)
        value = -value;
    return fromFraction(value);
}
