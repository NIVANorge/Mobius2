
#include <cmath>

#include "../external_computations.h"
/*




# Only works for one set of jets per basin...  (i.e. one release depth)
flux(layer.water, layer.water[vert.specific], [m 3, day-1], "Jet transport") {
	jet_transport
} @specific {
	jet_target
}

# With this we could have one per jet (where jet_c just connects the same basin to itself, but you get multiple edges this way
# Could then index the jet specification over that index set.

flux(layer.water, layer.water[jet_c.below, vert.specific], ..)


*/


// Indexes for the variables into the state and derivative vectors.
constexpr int N = 5; // Number states for the dynamical system
constexpr int volume_flux = 0;
constexpr int momentum_z = 1;
constexpr int buoyancy_flux = 2;
constexpr int x = 3;
constexpr int z = 4;

// Physics and shape constants
constexpr double pi = 3.141592653590;
constexpr double gravity = 9.81;
//constexpr double rho_scale = 1000.0;
constexpr double alpha = 0.057;
constexpr double lambda = 1.18;
constexpr double lambdasq = lambda*lambda;
constexpr double lambda_form_factor = (lambdasq + 1.0) / lambdasq;


// TODO: Probably pack additional data into a general void *data
void
jet_derivatives(double *v, double *d, double momentum_x, double drho_dz) {
	
	double momentum = std::sqrt(v[momentum_z]*v[momentum_z] + momentum_x*momentum_x) + 1e-20; //To handle initial phase if momentum_x = 0
	
	// TODO: Why the minus? Is v[momentum_z] negative?
	double sin_theta = -v[momentum_z]/momentum; // >0 for upward jet
	// TODO: Only need cos(theta). Faster to do cos_theta = sqrt(1 - sin_theta*sin_theta)?
	double theta = std::asin(sin_theta);
	double cos_theta = std::cos(theta);
	double velocity = 2.0*momentum/v[volume_flux]; // In center of jet
	double bsq = v[volume_flux] / (velocity * pi);
	double b = std::sqrt(bsq);
	double rhodiff = -(v[buoyancy_flux] / v[volume_flux])*lambda_form_factor;
	double drho_ds = -drho_dz*sin_theta;
	
	// Compute derivatives of state variables
	d[volume_flux]   = alpha * 2.0 * pi * b * velocity;
	d[momentum_z]    = pi * gravity * lambdasq * bsq * rhodiff / rho_scale;
	d[buoyancy_flux] = v[volume_flux] * drho_ds;
	d[z]             = -sin_theta;
	d[x]             = cos_theta;
}

bool
itp_step(double vn, double v, double b, double target, double *step, double step_done) {
	// Find reduced step for interpolating to specified value
	
	constexpr double rootsign[2] = { -1.0, 1.0 };
	
	bool result = false;
	
	// 2nd order approximation over step_done
	// v^(dS) = v + b*dS + (vn-v-b*step_done)*(ds/step_done)**2
	
	double a = (vn-v-b*step_done)/(step_done*step_done);
	double c = v-target;
	
	// TODO: Fix floating point equality and ineqauality checks ??
	
	if( a != 0 ) {
		double rootarg = b*b - 4*a*c;
		if( rootarg >= 0.0 ) {
			double root = std::sqrt(rootarg);
			for( double rootsgn : rootsign) {
				double step_limit = (-b + rootsgn*root)/(2.0*a);
				if (step_limit > 0.0 && step_limit <= *step) {
					result = true;
					*step = step_limit;
				}
			}
		}
	}
	
	// 1st order interpolation?
	if (!result) {
		if( (vn != v) && (vn - target)*(target - v) >= 0.0) {
			double step_limit = step_done*std::abs( (target-v)/(vnew-v) );
			if(step_limit > 0.0 && step_limit <= step) {
				result = true;
				*step = step_limit;
			}
		}
	}
	
	return result;
}

void
rk3_integrate(double z0, double z1, double drho_dz, double step_norm, double step_min, double step_max, int max_steps, double s, double *v, double *vn, double *d0, double *dVnorm, double *accuracy, double momentum_x, int *limit_exceeded, bool *neutral_point, int *step_count) {
	
	constexpr double coeff[2] = { 0.5, 0.75 };
	
	double step, step_factor, step_done;
	int point_iteration;
	
	*step_count = 0;
	double snew = s;
	
	double *d[3] = { &d0[0], &d0[N], &d0[2*N] };
	
	step_iteration :
		s = snew;
		jet_derivatives(v, d[0], momentum_x, drho_dz);
		
		// Accumulate maximum derivative scale for first 3 states (volume flux, momentum z, buoyancy flux)
		for(int k = 0; k < 3; ++k)
			dVnorm[k] = std::max(std::abs(v[k])/10.0, dVnorm[k]);
		
	error_control_repeat :
		
		step = std::max(step_min, std::min(step_max, step_norm));
		point_iteration = 0;
		
	point_iteration_reset :
		if (step_count >= max_steps) return;
		
		*step_count++;
		
		for(int k = 1; k < 3; ++k) {
			for(int i = 0; i < N; ++i) {
				vn[i] = v[i] + coeff[k]*step*d[k-1][i];
			}
			jet_derivatives(vn, d[k], momentum_x, drho_dz);
		}
		
		step_factor = 12.5*step_norm/step;
		snew = s + step;
		
		for(int i = 0; i < N; ++i) {
			// 2nd order estimate of new value
			vn[i] = v[i] + step*d[1][i]
			
			// 3rd order error over step
			double err3 = step * (2.0*d[0][i] - 6.0*d[1][i] + 4.0*d[2][i]) / 9.0;
			
			// Scale for permitted local error from maximum of
			//  - change of value over step or by norm, linear with step.
			double errl1 = std::max(std::abs(vn[i]-v[i]), std::abs(dvnorm[i]*step))*accuracy[i];
			
			// double integrated 2. deerivative, 2.order in step
			double errl2 = std::abs((vn[i] - (v[i]*d[0][i]*step)))*accuracy[i];
			
			// Adjust step as large a possible value, but with all errors within limits
			if(err3 >= 0.0) {
				step_factor = std::min( step_factor, std::max( std::sqrt(errl1/err3), errl2/err3 ));
			}
		}
		
		// In preparation for next step:
		
		// Factor 0.9 to reduce rejections and avoid infinite loop due to min. fluctuation in step.
		step_norm = 0.9*step_factor*step;
		
		if(step_factor < 1.0 && step > step_min && point_iteration == 0)
			goto error_control_repeat;
		
		step_done = step;
		
		*limit_exceeded = 0;
		// TODO: Implement ITP_STEP
		/*
			if(itp_step( .... )
				*limit_exceeded = 0;
			if(itp_step( .... )
				*limit_exceeded = 1;
		*/
		// *neutral_point = itp_step( .... );
		
		// TODO: make it into a loop instead of goto!!!!
		if( (step <= step_done - step_min) && point_iteration <= 2 ) {
			point_iteration++;
			goto point_iteration_reset;
		}
		
		for(int i = 0; i < N; ++i) {
			v[i] = vn[i];
		}
		
		if (*limit_exceeded == 0 && !*neutral_point)
			goto step_iteration;
		
}

void
nivafjord_compute_jet(Value_Access *values) {
	
	// out
	auto &q_transp      = values[0];  // Volume transported out of each layer
	auto &transp_idx    = values[1];  // Index of layer water is transported to.
	
	// in
	auto &z             = values[2];  // Depth limits for fjord layers (m)
	auto &dens          = values[3];  // Density of fjord layers (kg/m3)
	double outlet_z     = *values[4]; // Outlet depth (m)
	double outflux      = *values[5]; // Outlet volume flux (sum over jets) (m3/s)
	double outflux_dens = *values[6]; // Density of outlet water (kg/m3)
	double jet_diam     = *values[7]; // Initial diameter of each jet (m)
	s64 n_holes         = values[8].int_at(0); // Number of jets
	
	n_holes = std::max(1, n_holes);
	
	double jet_flux = outflux; // Changes if we add an intake.
	
	double jet_area = pi*jet_diam*jet_diam/4.0;
	double jet_velocity = jet_flux / ((double)n_holes * jet_area);
	double momentum_x = jet_area * jet_velocity * jet_velocity;
	double theta = 0.0; // Initial angle of jet wrt horizontal plane.
	
	double v[N]; // State vector for integration.
	
	
	// Initial state
	v[volume_flux] = 2.0*jet_area*jet_velocity;
	v[momentum_z] = 0.0;
	v[x] = 6.2*jet_diam*std::cos(theta);
	v[z] = outlet_z - 6.2*jet_diam*std::sin(theta);
	// v[buoyancy_flux]  -- see below (TODO)
	
	double prev_flux = jet_flux / (double)n_holes;
	

	// TODO integrate the differential equation until it hits a new layer or a neutral point (continue with new layer and so on)
	
	// Actually need to work with half-points between layer boundaries since that is where the pressure gradient changes.
	
	// Would be nice to have the half-points as a vector.
		// How bad is it to allocate/free inside this routine?
		// Alternatively we could make a function called get_scratchpad that calls into the framework, and hopefully it only allocates once.
	
	int outlet_idx = 0;
	
	double *half_z = (double *)malloc(sizeof(double)*(z.count + 1));
	half_z[0] = 0.5*z[0];
	for(int l = 1; l <= z.count; ++l) {
		half_z[l] = 0.5*(z[l-1] + z[l]);
		if(outlet_z < half_z[l]) {
			outlet_idx = l;
			break;
		}
	}
	
	// TODO: This doesn't work? We have to break at layer boundaries anyway to store out the volume flux for the layer.
	// How bad of an approximation would it be to say the gradient in the layer is the average of the gradient above and below the middle point?
	// It is anyway an approximation 
}