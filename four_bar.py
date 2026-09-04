import numpy as np


class FourBarMechanism:
    def __init__(self, a, b, c, d, omega_a=1.0, alpha_a=0.0):
        self.a = a
        self.b = b
        self.c = c
        self.d = d
        self.omega_a = omega_a
        self.alpha_a = alpha_a

        self.check_grashof()

    def check_grashof(self):
        links = sorted([self.a, self.b, self.c, self.d])
        s, p, q, l_link = links
        if s + l_link <= p + q:
            print(f"Grashof condition satisfied (s+l={s+l_link:.3f} <= p+q={p+q:.3f}) -> Mechanism is Grashof.")
        else:
            print(f"Grashof condition NOT satisfied (s+l={s+l_link:.3f} > p+q={p+q:.3f}) -> Non-Grashof (limited rocking).")

    def position_analysis(self, theta):
        a, b, c, d = self.a, self.b, self.c, self.d

        k = (a**2 - b**2 + c**2 + d**2) / 2.0

        A = k - a * (d - c) * np.cos(theta) - c * d
        B = -2 * a * c * np.sin(theta)
        C = k - a * (d + c) * np.cos(theta) + c * d

        disc = B**2 - 4 * A * C
        if disc < 0:
            return None, None

        sqrt_disc = np.sqrt(disc)

        phi1 = 2 * np.arctan2(-B + sqrt_disc, 2 * A)
        2 * np.arctan2(-B - sqrt_disc, 2 * A)

        phi = phi1

        sin_beta = (c * np.sin(phi) - a * np.sin(theta)) / b
        sin_beta = np.clip(sin_beta, -1.0, 1.0)
        beta = np.arcsin(sin_beta)

        return phi, beta

    def velocity_analysis(self, theta, phi, beta):
        a, b, c = self.a, self.b, self.c
        omega_a = self.omega_a

        denom_b = b * np.sin(phi - beta)
        denom_c = c * np.sin(beta - phi)

        if abs(denom_b) < 1e-9 or abs(denom_c) < 1e-9:
            return None, None

        omega_b = -a * omega_a * np.sin(theta - beta) / denom_b
        omega_c = a * omega_a * np.sin(theta - phi) / denom_c

        return omega_b, omega_c

    def acceleration_analysis(self, theta, phi, beta, omega_b, omega_c):
        a, b, c = self.a, self.b, self.c
        omega_a = self.omega_a
        alpha_a = self.alpha_a

        A_term = (a * alpha_a * np.sin(theta) + a * omega_a**2 * np.cos(theta)
                  + b * omega_b**2 * np.cos(phi) - c * omega_c**2 * np.cos(beta))
        B_term = (a * alpha_a * np.cos(theta) - a * omega_a**2 * np.sin(theta)
                  - b * omega_b**2 * np.sin(phi) + c * omega_c**2 * np.sin(beta))

        denom = np.sin(beta - phi)
        if abs(denom) < 1e-9:
            return None, None

        alpha_b = (A_term * np.cos(beta) + B_term * np.sin(beta)) / (b * denom)
        alpha_c = (A_term * np.cos(phi) + B_term * np.sin(phi)) / (c * denom)

        return alpha_b, alpha_c

    def full_analysis(self, theta_start_deg=0, theta_end_deg=360, step_deg=5):
        results = []
        thetas_deg = np.arange(theta_start_deg, theta_end_deg + step_deg, step_deg)

        for theta_deg in thetas_deg:
            theta = np.radians(theta_deg)

            phi, beta = self.position_analysis(theta)
            if phi is None:
                continue

            omega_b, omega_c = self.velocity_analysis(theta, phi, beta)
            if omega_b is None:
                continue

            alpha_b, alpha_c = self.acceleration_analysis(theta, phi, beta, omega_b, omega_c)

            results.append({
                'theta_deg': theta_deg,
                'phi_deg': np.degrees(phi),
                'beta_deg': np.degrees(beta),
                'omega_b': omega_b,
                'omega_c': omega_c,
                'alpha_b': alpha_b,
                'alpha_c': alpha_c
            })

        return results

    def print_table(self, results):
        header = f"{'theta(deg)':>10} {'phi(deg)':>10} {'beta(deg)':>10} {'omega_b':>10} {'omega_c':>10} {'alpha_b':>10} {'alpha_c':>10}"
        print(header)
        print("-" * len(header))
        for r in results:
            print(f"{r['theta_deg']:10.1f} {r['phi_deg']:10.3f} {r['beta_deg']:10.3f} "
                  f"{r['omega_b']:10.4f} {r['omega_c']:10.4f} "
                  f"{r['alpha_b']:10.4f} {r['alpha_c']:10.4f}")

if __name__ == "__main__":
    a = 2.0
    b = 6.0
    c = 5.0
    d = 7.0
    omega_a = 1.0
    alpha_a = 0.0

    mechanism = FourBarMechanism(a, b, c, d, omega_a, alpha_a)
    results = mechanism.full_analysis(theta_start_deg=300, theta_end_deg=310, step_deg=10)
    mechanism.print_table(results)
