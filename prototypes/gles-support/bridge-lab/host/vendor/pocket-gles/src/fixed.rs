//! Fixed-point conversion for the OpenGL ES `*x` entry points.
//!
//! OpenGL ES 1.x defines `GLfixed` as a signed 16.16 fixed-point value:
//! the low 16 bits are the fraction, the high 16 bits the integer part.
//! Every `gl*x` call (`glFogx`, `glLoadMatrixx`, `glFrustumx`, …) takes
//! its arguments in that format, and the `GL_FIXED` vertex-array type
//! stores coordinates the same way.
//!
//! This matters more than it looks. Windows Mobile devices had no FPU
//! worth using, so shipping games target the *Common-Lite* profile,
//! where the fixed-point entry points are the only ones that exist —
//! Call of Duty 2 drives its entire matrix and material pipeline
//! through `glLoadMatrixx` / `glFrustumx` / `glTexEnvx`. Desktop
//! OpenGL 2.1 has no fixed-point support at all, not even `GL_FIXED`
//! vertex arrays, so this conversion is unavoidable on the host side.

/// Number of fractional bits in a `GLfixed`.
pub const FRACTION_BITS: u32 = 16;
/// `1.0` expressed as a `GLfixed`.
pub const ONE: i32 = 1 << FRACTION_BITS;

/// Convert a 16.16 fixed-point value to `f32`.
#[inline]
pub fn to_f32(v: i32) -> f32 {
    v as f32 / ONE as f32
}

/// Convert an `f32` to 16.16 fixed-point, saturating at the
/// representable range instead of wrapping.
#[inline]
pub fn from_f32(v: f32) -> i32 {
    let scaled = v * ONE as f32;
    if scaled >= i32::MAX as f32 {
        i32::MAX
    } else if scaled <= i32::MIN as f32 {
        i32::MIN
    } else {
        scaled as i32
    }
}

/// Reinterpret a raw guest word as `GLfixed` and convert to `f32`.
///
/// Arguments arrive from the guest as `u32` register values; the
/// fixed-point ABI treats them as signed.
#[inline]
pub fn word_to_f32(raw: u32) -> f32 {
    to_f32(raw as i32)
}

/// Reinterpret a raw guest word as an IEEE-754 `GLfloat`.
///
/// The Common profile's `gl*f` entry points pass floats in integer
/// registers under the soft-float ABI, so they arrive bit-identical
/// in a `u32`.
#[inline]
pub fn word_to_f32_bits(raw: u32) -> f32 {
    f32::from_bits(raw)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn one_round_trips() {
        assert_eq!(to_f32(ONE), 1.0);
        assert_eq!(from_f32(1.0), ONE);
    }

    #[test]
    fn negative_values_are_signed() {
        // 0xFFFF0000 is -1.0 in 16.16, not a huge positive number.
        assert_eq!(word_to_f32(0xFFFF_0000), -1.0);
        assert_eq!(to_f32(-ONE), -1.0);
    }

    #[test]
    fn fraction_is_preserved() {
        assert_eq!(to_f32(ONE / 2), 0.5);
        assert_eq!(to_f32(ONE / 4), 0.25);
        assert_eq!(from_f32(0.5), ONE / 2);
    }

    #[test]
    fn saturates_instead_of_wrapping() {
        // A naive `(v * 65536.0) as i32` on a large input used to wrap
        // to a negative value, flipping the sign of a projection
        // matrix element.
        assert_eq!(from_f32(1.0e9), i32::MAX);
        assert_eq!(from_f32(-1.0e9), i32::MIN);
    }

    #[test]
    fn float_bits_are_not_reinterpreted_as_fixed() {
        // 1.0f32 has the bit pattern 0x3F800000. Read as 16.16 fixed
        // that would be 16256.0 — the classic mix-up between the `f`
        // and `x` entry points.
        let raw = 1.0f32.to_bits();
        assert_eq!(word_to_f32_bits(raw), 1.0);
        assert!((word_to_f32(raw) - 1.0).abs() > 1000.0);
    }
}
