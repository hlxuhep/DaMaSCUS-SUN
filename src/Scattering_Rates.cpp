#include "Scattering_Rates.hpp"

#include "libphysica/Integration.hpp"
#include "libphysica/Natural_Units.hpp"
#include "libphysica/Special_Functions.hpp"
#include "libphysica/Statistics.hpp"
#include "libphysica/Utilities.hpp"

namespace DaMaSCUS_SUN
{
using namespace libphysica::natural_units;
using namespace std::complex_literals;

std::complex<double> Plasma_Dispersion_Function(double x)
{
	double expmx2_erfi = 2.0 / std::sqrt(M_PI) * libphysica::Dawson_Integral(x);   // remove exp(-x^2) exp(x^2) to avoid inf value
	return std::sqrt(M_PI) * (1i * std::exp(-x * x) - expmx2_erfi);
}

std::complex<double> Polarization_Tensor_Longitudinal(double q0, double q, double temperature, double number_density, double mass, double Z)
{
	bool use_vlaslov_approximation = false;

	double sigma_MB				= std::sqrt(temperature / mass);
	double plasma_frequency_sqr = Elementary_Charge * Elementary_Charge * Z * Z * number_density / mass;
	double xi					= q0 / std::sqrt(2.0) / sigma_MB / q;
	double delta				= q / 2.0 / std::sqrt(2.0) / mass / sigma_MB;

	if(!use_vlaslov_approximation)
		return plasma_frequency_sqr * mass / q0 * xi * (Plasma_Dispersion_Function(xi - delta) - Plasma_Dispersion_Function(xi + delta));
	else
		return plasma_frequency_sqr / sigma_MB / sigma_MB * (1.0 + xi * Plasma_Dispersion_Function(xi));
}

template <typename Container>
std::complex<double> Polarization_Tensor_L(double q0, double q, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei)
{
	std::complex<double> PI_L = 0.0;
	// 1. Electron contributions
	PI_L += Polarization_Tensor_Longitudinal(q0, q, temperature, number_density_electrons, mElectron, 1.0);

	// 2. Nuclear contributions
	for(unsigned int i = 0; i < nuclei.size(); i++)
		PI_L += Polarization_Tensor_Longitudinal(q0, q, temperature, number_densities_nuclei[i], nuclei[i].mass, nuclei[i].Z);
	return PI_L;
}

template <typename Container>
double Form_Factor_Medium_Effects(double q0, double q, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei)
{
	std::complex<double> denominator = q * q + Polarization_Tensor_L(q0, q, temperature, number_density_electrons, nuclei, number_densities_nuclei);
	return q * q * q * q / std::norm(denominator);
}

// 1. Differential scattering rate dGamma / dq / dcos(theta)
template <typename Container>
double Differential_Scattering_Rate_Electron(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects)
{
	double prefactor = number_density_electrons * std::sqrt(2.0 / M_PI) * std::sqrt(mElectron / temperature);
	double k_1		 = DM.mass * vDM;
	if(use_medium_effects)
	{
		double q0 = 1.0 / 2.0 / DM.mass * (q * q + 2.0 * q * k_1 * cos_theta);
		prefactor *= Form_Factor_Medium_Effects(q0, q, temperature, number_density_electrons, nuclei, number_densities_nuclei);
	}
	double p1min = std::fabs(q / 2.0 * (1.0 + mElectron / DM.mass) + k_1 * mElectron / DM.mass * cos_theta);
	vDM			 = 1.0;	  // cancels in next expression (in case vDM == 0)
	return prefactor * q * DM.dSigma_dq2_Electron(q, vDM) * vDM * vDM * std::exp(-p1min * p1min / 2.0 / mElectron / temperature);
}

template <typename Container>
double Differential_Scattering_Rate_Nucleus(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects)
{
	double prefactor = nucleus_density * std::sqrt(2.0 / M_PI) * std::sqrt(nucleus.mass / temperature);
	double k_1		 = DM.mass * vDM;
	if(use_medium_effects)
	{
		double q0 = 1.0 / 2.0 / DM.mass * (q * q + 2.0 * q * k_1 * cos_theta);
		prefactor *= Form_Factor_Medium_Effects(q0, q, temperature, number_density_electrons, nuclei, number_densities_nuclei);
	}
	double p1min = std::fabs(q / 2.0 * (1.0 + nucleus.mass / DM.mass) + k_1 * nucleus.mass / DM.mass * cos_theta);
	vDM			 = 1.0;	  // cancels in next expression (in case vDM == 0)
	return prefactor * q * DM.dSigma_dq2_Nucleus(q, nucleus, vDM) * vDM * vDM * std::exp(-p1min * p1min / 2.0 / nucleus.mass / temperature);
}

// 2. Total scattering rate
template <typename Container>
double Total_Scattering_Rate_Electron(obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	if(DM.Is_Sigma_Total_V_Dependent() || use_medium_effects || qMin > 0.0)
	{
		std::function<double(double, double)> integrand = [&DM, vDM, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, use_medium_effects](double q, double cos_theta) {
			return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
		};

		double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
		return libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, 1.0);
	}
	else
	{
		double v_rel = Thermal_Averaged_Relative_Speed(temperature, mElectron, vDM);
		return number_density_electrons * DM.Sigma_Total_Electron(vDM) * v_rel;
	}
}

template <typename Container>
double Total_Scattering_Rate_Nucleus(obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	if(DM.Is_Sigma_Total_V_Dependent() || use_medium_effects || qMin > 0.0)
	{
		std::function<double(double, double)> integrand = [&nucleus, nucleus_density, &DM, vDM, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, use_medium_effects](double q, double cos_theta) {
			return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
		};
		double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
		return libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, 1.0);
	}
	else
	{
		double v_rel = Thermal_Averaged_Relative_Speed(temperature, nucleus.mass, vDM);
		return nucleus_density * DM.Sigma_Total_Nucleus(nucleus, vDM) * v_rel;
	}
}

// 3. Thermal average of relative speed between a particle of speed v_DM and a solar thermal target.
double Thermal_Averaged_Relative_Speed(double temperature, double mass_target, double v_DM)
{
	double kappa = sqrt(mass_target / 2.0 / temperature);
	if(v_DM < 1.0e-20)
		return 2.0 / sqrt(M_PI) / kappa;
	else
	{
		double relative_speed = (1.0 + 2.0 * pow(kappa * v_DM, 2.0)) * erf(kappa * v_DM) / 2.0 / kappa / kappa / v_DM + exp(-pow(kappa * v_DM, 2.0)) / sqrt(M_PI) / kappa;
		return relative_speed;
	}
}

double Maximum_Relative_Speed(double temperature, double mass_target, double vDM, double N)
{
	double kappa = sqrt(mass_target / 2.0 / temperature);
	return vDM + N / kappa;
}

double Maximum_Momentum_Transfer(double mDM, double temperature, double mass_target, double vDM, double N)
{
	double vRelMax = Maximum_Relative_Speed(temperature, mass_target, vDM, N);
	return 2.0 * libphysica::Reduced_Mass(mDM, mass_target) * vRelMax;
}

// PDFs and sampling functions of cos(theta) and q (WILL BE MOVED TO ANOTHER FILE)
template <typename Container>
double PDF_Cos_Theta_Electron(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	// 1. Obtain dGamma/dcos_theta
	std::function<double(double)> integrand = [temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, cos_theta](double q) {
		return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};
	double qMax				= Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
	double dGamma_dcostheta = libphysica::Integrate(integrand, qMin, qMax);

	// 2. Compute total rate for normalization
	double Gamma = Total_Scattering_Rate_Electron(DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	return dGamma_dcostheta / Gamma;
}

template <typename Container>
double PDF_Cos_Theta_Nucleus(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	// 1. Obtain dGamma/dcos_theta
	std::function<double(double)> integrand = [&nucleus, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, nucleus_density, &DM, vDM, use_medium_effects, cos_theta](double q) {
		return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};
	double qMax				= Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
	double dGamma_dcostheta = libphysica::Integrate(integrand, qMin, qMax, "Gauss-Kronrod");

	// 2. Compute total rate for normalization
	double Gamma = Total_Scattering_Rate_Nucleus(DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	return dGamma_dcostheta / Gamma;
}

template <typename Container>
double CDF_Cos_Theta_Electron(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	if(cos_theta == -1.0)
		return 0.0;
	else if(cos_theta == 1.0)
		return 1.0;

	std::function<double(double, double)> integrand = [&DM, vDM, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, use_medium_effects](double q, double cos_theta) {
		return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};

	double qMax		   = Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
	double numerator   = libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, cos_theta);
	double denominator = libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, 1.0);
	return numerator / denominator;
}

template <typename Container>
double CDF_Cos_Theta_Nucleus(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	if(cos_theta == -1.0)
		return 0.0;
	else if(cos_theta == 1.0)
		return 1.0;

	std::function<double(double, double)> integrand = [&nucleus, nucleus_density, &DM, vDM, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, use_medium_effects](double q, double cos_theta) {
		return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};
	double qMax		   = Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
	double numerator   = libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, cos_theta, "Gauss-Kronrod");
	double denominator = libphysica::Integrate_2D(integrand, qMin, qMax, -1.0, 1.0, "Gauss-Kronrod");
	return numerator / denominator;
}

// Conditional PDF/CDF of q for a fixed value of cos_theta
template <typename Container>
double PDF_q_Electron(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	// 1. Obtain dGamma/dcos_theta
	std::function<double(double)> integrand = [temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, cos_theta](double q) {
		return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};
	double qMax				= Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
	double dGamma_dcostheta = libphysica::Integrate(integrand, qMin, qMax);

	return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects) / dGamma_dcostheta;
}

template <typename Container>
double PDF_q_Nucleus(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	// 1. Obtain dGamma/dcos_theta
	std::function<double(double)> integrand = [&nucleus, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, nucleus_density, &DM, vDM, use_medium_effects, cos_theta](double q) {
		return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
	};
	double qMax				= Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
	double dGamma_dcostheta = libphysica::Integrate(integrand, qMin, qMax, "Gauss-Kronrod");
	return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects) / dGamma_dcostheta;
}

template <typename Container>
double CDF_q_Electron(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
	if(q == qMin)
		return 0.0;
	else if(q == qMax)
		return 1.0;
	else
	{
		std::function<double(double)> integrand = [cos_theta, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects](double q) {
			return Differential_Scattering_Rate_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
		};
		double numerator   = libphysica::Integrate(integrand, qMin, q);
		double denominator = libphysica::Integrate(integrand, qMin, qMax);
		return numerator / denominator;
	}
}

template <typename Container>
double CDF_q_Nucleus(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
	if(q == qMin)
		return 0.0;
	else if(q == qMax)
		return 1.0;
	else
	{
		std::function<double(double)> integrand = [cos_theta, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &nucleus, nucleus_density, &DM, vDM, use_medium_effects](double q) {
			return Differential_Scattering_Rate_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects);
		};
		double numerator   = libphysica::Integrate(integrand, qMin, q, "Gauss-Kronrod");
		double denominator = libphysica::Integrate(integrand, qMin, qMax, "Gauss-Kronrod");
		return numerator / denominator;
	}
}

template <typename Container>
double Sample_Cos_Theta_Electron(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	// std::function<double(double)> cdf = [temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, qMin](double cos) {
	// 	return CDF_Cos_Theta_Electron(cos, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	// };
	// return libphysica::Inverse_Transform_Sampling(cdf, -1.0, 1.0, PRNG);
	std::function<double(double)> pdf = [temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, qMin](double cos) {
		return PDF_Cos_Theta_Electron(cos, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	};
	return libphysica::Rejection_Sampling(pdf, -1.0, 1.0, pdf(-1.0), PRNG);
}

template <typename Container>
double Sample_Cos_Theta_Nucleus(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	std::function<double(double)> pdf = [&nucleus, nucleus_density, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, qMin](double cos) {
		return PDF_Cos_Theta_Nucleus(cos, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	};
	return libphysica::Rejection_Sampling(pdf, -1.0, 1.0, pdf(-1.0), PRNG);
}

template <typename Container>
double Sample_q_Electron(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	std::function<double(double)> cdf = [cos_theta, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, qMin](double q) {
		return CDF_q_Electron(q, cos_theta, DM, vDM, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	};
	double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, mElectron, vDM);
	return libphysica::Inverse_Transform_Sampling(cdf, qMin, qMax, PRNG);
}

template <typename Container>
double Sample_q_Nucleus(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, Container& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin)
{
	std::function<double(double)> cdf = [&nucleus, cos_theta, nucleus_density, temperature, number_density_electrons, &nuclei, &number_densities_nuclei, &DM, vDM, use_medium_effects, qMin](double q) {
		return CDF_q_Nucleus(q, cos_theta, DM, vDM, nucleus, nucleus_density, temperature, number_density_electrons, nuclei, number_densities_nuclei, use_medium_effects, qMin);
	};
	double qMax = Maximum_Momentum_Transfer(DM.mass, temperature, nucleus.mass, vDM);
	return libphysica::Inverse_Transform_Sampling(cdf, qMin, qMax, PRNG);
}

// Explicitly instantiate the template functions with the two possible containers
// This avoids the need to implement the template functions in the header file
template std::complex<double> Polarization_Tensor_L<std::vector<obscura::Isotope>>(double q0, double q, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei);
template std::complex<double> Polarization_Tensor_L<std::vector<Solar_Isotope>>(double q0, double q, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei);

template double Form_Factor_Medium_Effects<std::vector<obscura::Isotope>>(double q0, double q, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei);
template double Form_Factor_Medium_Effects<std::vector<Solar_Isotope>>(double q0, double q, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei);

template double Total_Scattering_Rate_Electron<std::vector<obscura::Isotope>>(obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Total_Scattering_Rate_Electron<std::vector<Solar_Isotope>>(obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double Total_Scattering_Rate_Nucleus<std::vector<obscura::Isotope>>(obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Total_Scattering_Rate_Nucleus<std::vector<Solar_Isotope>>(obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double PDF_Cos_Theta_Electron<std::vector<obscura::Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double PDF_Cos_Theta_Electron<std::vector<Solar_Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double PDF_Cos_Theta_Nucleus<std::vector<obscura::Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double PDF_Cos_Theta_Nucleus<std::vector<Solar_Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double CDF_Cos_Theta_Electron<std::vector<obscura::Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double CDF_Cos_Theta_Electron<std::vector<Solar_Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double CDF_Cos_Theta_Nucleus<std::vector<obscura::Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double CDF_Cos_Theta_Nucleus<std::vector<Solar_Isotope>>(double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double PDF_q_Electron<std::vector<obscura::Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double PDF_q_Electron<std::vector<Solar_Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double PDF_q_Nucleus<std::vector<obscura::Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double PDF_q_Nucleus<std::vector<Solar_Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double CDF_q_Electron<std::vector<obscura::Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double CDF_q_Electron<std::vector<Solar_Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double CDF_q_Nucleus<std::vector<obscura::Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double CDF_q_Nucleus<std::vector<Solar_Isotope>>(double q, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double Sample_Cos_Theta_Electron<std::vector<obscura::Isotope>>(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Sample_Cos_Theta_Electron<std::vector<Solar_Isotope>>(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double Sample_Cos_Theta_Nucleus<std::vector<obscura::Isotope>>(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Sample_Cos_Theta_Nucleus<std::vector<Solar_Isotope>>(std::mt19937& PRNG, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double Sample_q_Electron<std::vector<obscura::Isotope>>(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Sample_q_Electron<std::vector<Solar_Isotope>>(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

template double Sample_q_Nucleus<std::vector<obscura::Isotope>>(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<obscura::Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);
template double Sample_q_Nucleus<std::vector<Solar_Isotope>>(std::mt19937& PRNG, double cos_theta, obscura::DM_Particle& DM, double vDM, obscura::Isotope& nucleus, double nucleus_density, double temperature, double number_density_electrons, std::vector<Solar_Isotope>& nuclei, std::vector<double>& number_densities_nuclei, bool use_medium_effects, double qMin);

}	// namespace DaMaSCUS_SUN