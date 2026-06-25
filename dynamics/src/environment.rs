use nalgebra::Vector3;

const EARTH_RADIUS: f64 = 6378137.0;
const AU: f64 = 1.495978707e11;

/// Earth's gravitational constant (m^3/s^2).
pub fn earth_gravitational_constant() -> f64 {
    3.986004418e14
}

/// J2 zonal harmonic coefficient.
pub fn j2_perturbation() -> f64 {
    1.08263e-3
}

/// Exponential atmosphere density model returning kg/m^3 at a given altitude (m).
pub fn atmosphere_density(altitude_m: f64) -> f64 {
    if altitude_m <= 0.0 {
        return 1.225;
    }

    let levels: [(f64, f64); 13] = [
        (0.0, 1.225),
        (25_000.0, 3.899e-2),
        (50_000.0, 1.027e-3),
        (75_000.0, 3.461e-5),
        (100_000.0, 5.604e-7),
        (125_000.0, 1.454e-8),
        (150_000.0, 2.074e-9),
        (175_000.0, 5.465e-10),
        (200_000.0, 1.634e-10),
        (300_000.0, 8.753e-12),
        (400_000.0, 1.428e-12),
        (500_000.0, 3.421e-13),
        (1_000_000.0, 2.179e-15),
    ];

    if altitude_m >= levels[levels.len() - 1].0 {
        return levels[levels.len() - 1].1;
    }

    for i in 0..levels.len() - 1 {
        let (h1, rho1) = levels[i];
        let (h2, rho2) = levels[i + 1];
        if altitude_m < h2 {
            let h = (h2 - h1) / (rho1 / rho2).ln();
            return rho1 * (-(altitude_m - h1) / h).exp();
        }
    }

    levels[levels.len() - 1].1
}

/// Simplified dipole Earth magnetic field in ECI (Tesla).
pub fn earth_magnetic_field_eci(position_eci: Vector3<f64>) -> Vector3<f64> {
    let r = position_eci.norm();
    if r < 1.0 {
        return Vector3::zeros();
    }
    let r_hat = position_eci / r;
    let b0 = 1e-7 * 7.94e22 / r.powi(3);
    let m_dot_r = -r_hat.z;
    Vector3::new(
        b0 * 3.0 * m_dot_r * r_hat.x,
        b0 * 3.0 * m_dot_r * r_hat.y,
        b0 * (3.0 * m_dot_r * r_hat.z + 1.0),
    )
}

/// Simplified solar ephemeris returning sun position in ECI (m).
pub fn sun_position_eci(julian_date: f64) -> Vector3<f64> {
    let d = julian_date - 2451545.0;

    let m = (357.5291 + 0.98560028 * d).rem_euclid(360.0);
    let m_rad = m.to_radians();
    let c = 1.9148 * m_rad.sin() + 0.0200 * (2.0 * m_rad).sin() + 0.0003 * (3.0 * m_rad).sin();

    let l = (280.46061837 + 0.98564736629 * d).rem_euclid(360.0);
    let lambda = (l + c).rem_euclid(360.0);
    let lambda_rad = lambda.to_radians();

    let eps = (23.439 - 0.00000036 * d).rem_euclid(360.0);
    let eps_rad = eps.to_radians();

    Vector3::new(
        AU * lambda_rad.cos(),
        AU * lambda_rad.sin() * eps_rad.cos(),
        AU * lambda_rad.sin() * eps_rad.sin(),
    )
}

/// Solar flux at a given ECI position (W/m^2).
pub fn solar_flux(position_eci: Vector3<f64>, sun_pos: Vector3<f64>) -> f64 {
    let solar_constant = 1361.0;
    let d = (position_eci - sun_pos).norm();
    solar_constant * (AU / d).powi(2)
}

/// Check whether the position is in Earth's umbra (cylindrical shadow model).
pub fn eclipse_check(position_eci: Vector3<f64>, sun_pos: Vector3<f64>) -> bool {
    let sun_dir = sun_pos.normalize();
    if position_eci.dot(&sun_dir) > 0.0 {
        return false;
    }
    let perp = position_eci.cross(&sun_dir).norm();
    perp < EARTH_RADIUS
}
