//! Number formatting compatible with the default C++ `std::ostream` output (`%g`, precision 6),
//! so CSV reports stay byte-identical to the C++ tool.

/// Formats `value` like `printf("%g", value)`.
pub fn format_g(value: f64) -> String {
    const PRECISION: i32 = 6;

    if value.is_nan() {
        return if value.is_sign_negative() {
            "-nan".into()
        } else {
            "nan".into()
        };
    }
    if value.is_infinite() {
        return if value < 0.0 { "-inf".into() } else { "inf".into() };
    }
    if value == 0.0 {
        return if value.is_sign_negative() {
            "-0".into()
        } else {
            "0".into()
        };
    }

    // Round to PRECISION significant digits first; the rounded exponent picks the style.
    let scientific = format!("{:.*e}", (PRECISION - 1) as usize, value);
    let (mantissa, exponent) = scientific.split_once('e').expect("exponent in {:e} output");
    let exponent: i32 = exponent.parse().expect("integer exponent");

    if !(-4..PRECISION).contains(&exponent) {
        let mantissa = trim_fraction(mantissa);
        let sign = if exponent < 0 { '-' } else { '+' };
        format!("{mantissa}e{sign}{:02}", exponent.abs())
    } else {
        let decimals = (PRECISION - 1 - exponent) as usize;
        trim_fraction(&format!("{value:.decimals$}")).to_owned()
    }
}

fn trim_fraction(text: &str) -> &str {
    if text.contains('.') {
        text.trim_end_matches('0').trim_end_matches('.')
    } else {
        text
    }
}

#[cfg(test)]
mod tests {
    use super::format_g;

    #[test]
    fn matches_printf_g() {
        let cases = [
            (0.0, "0"),
            (100.0, "100"),
            (49.85692, "49.8569"),
            (369.6334, "369.633"),
            (9.375, "9.375"),
            (0.5, "0.5"),
            (-2.5, "-2.5"),
            (123456.0, "123456"),
            (1234567.0, "1.23457e+06"),
            (999999.5, "1e+06"),
            (0.0001, "0.0001"),
            (0.00001234, "1.234e-05"),
            (33.84549999, "33.8455"),
        ];
        for (value, expected) in cases {
            assert_eq!(format_g(value), expected, "value {value}");
        }
    }
}
