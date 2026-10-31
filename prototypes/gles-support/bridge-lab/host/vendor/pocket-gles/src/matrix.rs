//! 4×4 matrix stack for the OpenGL ES 1.x fixed-function pipeline.
//!
//! Matrices are stored in OpenGL's column-major order: element `(row,
//! col)` lives at index `col * 4 + row`. That is the layout
//! `glLoadMatrixf` / `glLoadMatrixx` expect, so a guest-supplied matrix
//! can be converted element-wise without transposing.

/// A 4×4 matrix in OpenGL column-major order.
pub type Matrix4 = [f32; 16];

/// The 4×4 identity matrix.
pub const IDENTITY: Matrix4 = [
    1.0, 0.0, 0.0, 0.0, //
    0.0, 1.0, 0.0, 0.0, //
    0.0, 0.0, 1.0, 0.0, //
    0.0, 0.0, 0.0, 1.0, //
];

/// Matrix targets selectable with `glMatrixMode`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MatrixMode {
    Modelview,
    Projection,
    Texture,
    Palette,
}

impl MatrixMode {
    /// Map a `GL_MODELVIEW` / `GL_PROJECTION` / `GL_TEXTURE` enum to a
    /// target. Returns `None` for anything else, which the caller
    /// should report as `GL_INVALID_ENUM`.
    pub fn from_enum(value: u32) -> Option<Self> {
        match value {
            super::consts::GL_MODELVIEW => Some(Self::Modelview),
            super::consts::GL_PROJECTION => Some(Self::Projection),
            super::consts::GL_TEXTURE => Some(Self::Texture),
            0x8840 => Some(Self::Palette),
            _ => None,
        }
    }
}

/// Multiply two column-major 4×4 matrices, returning `a * b`.
///
/// OpenGL's `glMultMatrix` post-multiplies the current matrix, i.e.
/// `current = current * m`, so callers pass the current matrix as `a`.
pub fn multiply(a: &Matrix4, b: &Matrix4) -> Matrix4 {
    let mut out = [0.0f32; 16];
    for col in 0..4 {
        for row in 0..4 {
            let mut sum = 0.0;
            for k in 0..4 {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
    out
}

/// Build the `glFrustum` projection matrix.
pub fn frustum(l: f32, r: f32, b: f32, t: f32, n: f32, f: f32) -> Matrix4 {
    let mut m = [0.0f32; 16];
    m[0] = 2.0 * n / (r - l);
    m[5] = 2.0 * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[10] = -(f + n) / (f - n);
    m[11] = -1.0;
    m[14] = -2.0 * f * n / (f - n);
    m
}

/// Build the `glOrtho` projection matrix.
pub fn ortho(l: f32, r: f32, b: f32, t: f32, n: f32, f: f32) -> Matrix4 {
    let mut m = IDENTITY;
    m[0] = 2.0 / (r - l);
    m[5] = 2.0 / (t - b);
    m[10] = -2.0 / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m
}

/// Build a `glTranslate` matrix.
pub fn translate(x: f32, y: f32, z: f32) -> Matrix4 {
    let mut m = IDENTITY;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m
}

/// Build a `glScale` matrix.
pub fn scale(x: f32, y: f32, z: f32) -> Matrix4 {
    let mut m = IDENTITY;
    m[0] = x;
    m[5] = y;
    m[10] = z;
    m
}

/// Build a `glRotate` matrix — `angle` in degrees about axis `(x,y,z)`.
pub fn rotate(angle_degrees: f32, x: f32, y: f32, z: f32) -> Matrix4 {
    let len = (x * x + y * y + z * z).sqrt();
    if len == 0.0 {
        return IDENTITY;
    }
    let (x, y, z) = (x / len, y / len, z / len);
    let rad = angle_degrees.to_radians();
    let c = rad.cos();
    let s = rad.sin();
    let ic = 1.0 - c;
    let mut m = IDENTITY;
    m[0] = x * x * ic + c;
    m[1] = y * x * ic + z * s;
    m[2] = x * z * ic - y * s;
    m[4] = x * y * ic - z * s;
    m[5] = y * y * ic + c;
    m[6] = y * z * ic + x * s;
    m[8] = x * z * ic + y * s;
    m[9] = y * z * ic - x * s;
    m[10] = z * z * ic + c;
    m
}

/// Transform a 4-component column vector by a column-major matrix.
pub fn transform(m: &Matrix4, v: [f32; 4]) -> [f32; 4] {
    let mut out = [0.0f32; 4];
    for row in 0..4 {
        out[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
    }
    out
}

/// One matrix stack (current matrix plus saved copies).
///
/// OpenGL ES 1.1 guarantees a modelview stack at least 16 deep and
/// projection / texture stacks at least 2 deep. We use a single
/// generous limit for all three; exceeding it must raise
/// `GL_STACK_OVERFLOW` rather than grow without bound.
#[derive(Debug, Clone)]
pub struct MatrixStack {
    current: Matrix4,
    saved: Vec<Matrix4>,
    depth_limit: usize,
}

impl Default for MatrixStack {
    fn default() -> Self {
        Self::new(16)
    }
}

impl MatrixStack {
    pub fn new(depth_limit: usize) -> Self {
        Self {
            current: IDENTITY,
            saved: Vec::new(),
            depth_limit,
        }
    }

    pub fn current(&self) -> &Matrix4 {
        &self.current
    }

    pub fn load(&mut self, m: Matrix4) {
        self.current = m;
    }

    pub fn load_identity(&mut self) {
        self.current = IDENTITY;
    }

    /// Post-multiply the current matrix by `m` (`current = current * m`).
    pub fn multiply_by(&mut self, m: &Matrix4) {
        self.current = multiply(&self.current, m);
    }

    /// Push a copy of the current matrix. Returns `false` on overflow,
    /// which the caller reports as `GL_STACK_OVERFLOW`.
    pub fn push(&mut self) -> bool {
        if self.saved.len() + 1 >= self.depth_limit {
            return false;
        }
        self.saved.push(self.current);
        true
    }

    /// Restore the most recently pushed matrix. Returns `false` on
    /// underflow, reported as `GL_STACK_UNDERFLOW`.
    pub fn pop(&mut self) -> bool {
        match self.saved.pop() {
            Some(m) => {
                self.current = m;
                true
            }
            None => false,
        }
    }

    pub fn depth(&self) -> usize {
        self.saved.len() + 1
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_close(a: &Matrix4, b: &Matrix4) {
        for (i, (x, y)) in a.iter().zip(b.iter()).enumerate() {
            assert!((x - y).abs() < 1e-5, "element {i}: {x} != {y}");
        }
    }

    #[test]
    fn identity_is_multiplicative_unit() {
        let m = translate(1.0, 2.0, 3.0);
        assert_close(&multiply(&m, &IDENTITY), &m);
        assert_close(&multiply(&IDENTITY, &m), &m);
    }

    #[test]
    fn translation_moves_a_point() {
        let m = translate(10.0, 20.0, 30.0);
        let p = transform(&m, [1.0, 2.0, 3.0, 1.0]);
        assert_eq!(p, [11.0, 22.0, 33.0, 1.0]);
    }

    #[test]
    fn matrix_order_is_column_major() {
        // Column-major means the translation lives in elements 12..15.
        // Getting this backwards transposes every matrix the guest
        // uploads and sends geometry off-screen.
        let m = translate(7.0, 8.0, 9.0);
        assert_eq!(m[12], 7.0);
        assert_eq!(m[13], 8.0);
        assert_eq!(m[14], 9.0);
    }

    #[test]
    fn multiplication_is_not_commutative_and_matches_gl_order() {
        // glTranslate then glScale must scale in the translated frame:
        // current = T * S, so a point is scaled first, then translated.
        let t = translate(10.0, 0.0, 0.0);
        let s = scale(2.0, 2.0, 2.0);
        let ts = multiply(&t, &s);
        let p = transform(&ts, [1.0, 0.0, 0.0, 1.0]);
        assert_eq!(p[0], 12.0);
        // The other order scales the translation itself.
        let st = multiply(&s, &t);
        let q = transform(&st, [1.0, 0.0, 0.0, 1.0]);
        assert_eq!(q[0], 22.0);
    }

    #[test]
    fn rotation_by_90_degrees_about_z() {
        let m = rotate(90.0, 0.0, 0.0, 1.0);
        let p = transform(&m, [1.0, 0.0, 0.0, 1.0]);
        assert!((p[0] - 0.0).abs() < 1e-5);
        assert!((p[1] - 1.0).abs() < 1e-5);
    }

    #[test]
    fn frustum_maps_near_plane_to_minus_one() {
        let m = frustum(-1.0, 1.0, -1.0, 1.0, 1.0, 100.0);
        // A point on the near plane maps to z_ndc = -1 after the
        // perspective divide.
        let p = transform(&m, [0.0, 0.0, -1.0, 1.0]);
        assert!((p[2] / p[3] - -1.0).abs() < 1e-5);
    }

    #[test]
    fn ortho_maps_corners_to_unit_cube() {
        let m = ortho(0.0, 240.0, 320.0, 0.0, -1.0, 1.0);
        let tl = transform(&m, [0.0, 0.0, 0.0, 1.0]);
        assert!((tl[0] - -1.0).abs() < 1e-5);
        assert!((tl[1] - 1.0).abs() < 1e-5);
    }

    #[test]
    fn push_pop_restores_the_matrix() {
        let mut s = MatrixStack::default();
        s.load(translate(1.0, 0.0, 0.0));
        assert!(s.push());
        s.load_identity();
        assert_eq!(s.current()[12], 0.0);
        assert!(s.pop());
        assert_eq!(s.current()[12], 1.0);
    }

    #[test]
    fn pop_on_empty_stack_reports_underflow() {
        let mut s = MatrixStack::default();
        assert!(!s.pop());
    }

    #[test]
    fn push_beyond_limit_reports_overflow() {
        let mut s = MatrixStack::new(4);
        assert!(s.push());
        assert!(s.push());
        assert!(s.push());
        // The 4th push would exceed the documented depth.
        assert!(!s.push());
    }
}
