
/*
Original FORTRAN jetmix code by Birger Bjerkeng.
Adapted to C++ as a Mobius2 plugin by Magnus Dahler Norling.
*/


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
constexpr int ix = 3;
constexpr int iz = 4;

constexpr int max_steps = 200;
constexpr double step_min = 0.001;
constexpr double step_max = 10.0;

// Physics and shape constants
constexpr double pi = 3.141592653590;
constexpr double gravity = 9.81;
//constexpr double rho_scale = 1000.0;
constexpr double alpha = 0.057;
constexpr double lambda = 1.18;
constexpr double lambdasq = lambda*lambda;
constexpr double lambda_form_factor = (lambdasq + 1.0) / lambdasq;


static void
jet_derivatives(double *v, double *d, double momentum_x, double drho_dz) {
	
	constexpr double rho_scale = 1000.0;
	
	// TODO: Why is [ix] even tracked? It is not used in the computation.
	
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
	d[iz]            = -sin_theta;
	d[ix]            = cos_theta;
}

static bool
itp_step(double vn, double v, double b, double target, double *step, double step_done) {
	// Find reduced step for interpolating to specified value
	
	constexpr double rootsign[2] = { -1.0, 1.0 };
	
	bool result = false;
	
	// 2nd order approximation over step_done
	// v^(dS) = v + b*dS + (vn-v-b*step_done)*(ds/step_done)**2
	
	double a = (vn-v-b*step_done)/(step_done*step_done);
	double c = v-target;
	
	// TODO: Avoid floating point equality and inequality checks ??
	
	if( a != 0.0 ) {
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
			double step_limit = step_done*std::abs( (target-v)/(vn-v) );
			if(step_limit > 0.0 && step_limit <= *step) {
				result = true;
				*step = step_limit;
			}
		}
	}
	
	return result;
}

static void
rk3_integrate(double z0, double z1, double drho_dz, double step_norm, double s, double *v, double *vn, double *d0, double *dvnorm, double *accuracy, double momentum_x, int *limit_exceeded, bool *neutral_point, int *step_count) {
	
	constexpr double coeff[2] = { 0.5, 0.75 };
	
	double step, step_factor, step_done;
	int point_iteration;
	
	*step_count = 0;
	double snew = s;
	
	double *d[3] = { &d0[0], &d0[N], &d0[2*N] };
	
	// Main integrator step iteration
	while(true) {
		s = snew;
		jet_derivatives(v, d[0], momentum_x, drho_dz);
		
		// Accumulate maximum derivative scale for first 3 states (volume flux, momentum z, buoyancy flux)
		for(int k = 0; k < 3; ++k)
			dvnorm[k] = std::max(std::abs(v[k])/10.0, dvnorm[k]);
			
		
		error_control_repeat :
			
			step = std::max(step_min, std::min(step_max, step_norm));
			point_iteration = 0;
			
		point_iteration_reset :
			if (*step_count >= max_steps) {
				//log_print("Max step count exceeded!\n");
				return;
			}
			
			(*step_count)++;
			
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
				vn[i] = v[i] + step*d[1][i];
				
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
			
			*limit_exceeded = -1;
			
			if(itp_step(vn[iz], v[iz], d[0][iz], z0, &step, step_done))
				*limit_exceeded = 0;
			if(itp_step(vn[iz], v[iz], d[0][iz], z1, &step, step_done))
				*limit_exceeded = 1;
			
			// Neutral point, i.e. buoyancy flux is 0
			*neutral_point = itp_step(vn[buoyancy_flux], v[buoyancy_flux], d[0][buoyancy_flux], 0.0, &step, step_done);
			if(*neutral_point)
				*limit_exceeded = -1;
			
			// TODO: make it into a loop instead of goto!!!!
			if( (step <= step_done - step_min) && point_iteration <= 2 ) {
				point_iteration++;
				goto point_iteration_reset;
			}
			
			for(int i = 0; i < N; ++i) {
				v[i] = vn[i];
			}
		
		if ( (*limit_exceeded >= 0) || *neutral_point)
			break;
	}
		
}

extern "C" DLLEXPORT void
nivafjord_compute_jet_mixing(Value_Access *values) {
	
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
	
	//n_holes = std::max((s64)1, n_holes);
	if(n_holes < 1) return;
	
	double jet_flux = std::max(0.0, outflux); // Changes if we add an intake.
	
	double jet_area = pi*jet_diam*jet_diam/4.0;
	double jet_velocity = jet_flux / ((double)n_holes * jet_area);
	double momentum_x = jet_area * jet_velocity * jet_velocity;
	double theta = 0.0; // Initial angle of jet wrt horizontal plane.
	
	double v[N]; // State vector for integration.
	double vn[N]; // Working state vector for integrator
	double dvnorm[N];
	double d[N*3]; // Working space for state gradient computation.
	double accuracy[N];
	
	double accnorm = 0.001;
	for(int i = 0; i < N; ++i)
		accuracy[i] = accnorm;
	
	// Initial state
	v[volume_flux] = 2.0*jet_area*jet_velocity;
	v[momentum_z] = 0.0;
	v[ix] = 6.2*jet_diam*std::cos(theta);
	v[iz] = outlet_z - 6.2*jet_diam*std::sin(theta);
	// v[buoyancy_flux]  -- initialized below
	
	double step_norm = 1.0;
	double sum_steps = 0.0;
	double s = 0.0;  // Integrated distance along the jet trajectory.
	
	double prev_flux = jet_flux / (double)n_holes;
	
	bool first_step = true;
	

	double amb_dens_i, amb_depth;
// TODO: Rewrite to not use goto
iterate_layer :

	// TODO: This search can be optimized to start from current layer if we initially set it to the outlet layer.
	int layer = 0;
	for(int i = 0; i < z.count; ++i) {
		if(z[i] > v[iz]) {
			layer = i;
			break;
		}
	}
	
	double middle_z = 0.0;
	if(layer == 0)
		middle_z = z[0]*0.5;
	else
		middle_z = 0.5*(z[layer-1]+z[layer]);
	
iterate_half_layer :
	
	int l_above, l_below;
	double depth_tolerance;
	if(v[iz] <= middle_z) {
		l_above = std::max(0, layer-1);
		l_below = layer;
		depth_tolerance = 0.01;
	} else {
		l_above = layer;
		l_below = std::min(layer+1, (int)z.count-1);
		depth_tolerance = -0.01;
	}
	
	// Layer depth limits (iterval slitghtly expanded to ensure that step integration at depth limits gets beyond actual limit)
	// TODO: But this contracts the interval, it doesn't expand it??
	double z0 = middle_z + depth_tolerance;
	double z1 = z[l_above] - depth_tolerance;
	
	double drho_dz = 2.0*(dens[l_above] - dens[l_below])/(z[std::max(l_above-1, 0)] - z[l_below]);
	
	double amb_dens = dens[layer] + drho_dz*(v[iz] - middle_z);
	
	if(first_step) {
		
		v[buoyancy_flux] = 0.5*v[volume_flux]*(amb_dens - outflux_dens);
		
		// TODO: Not that good to do exact == comparison on floating point..
		if(v[buoyancy_flux] == 0.0 && v[momentum_z] == 0.0)
			goto finish_and_store_out;
		
		amb_dens_i = amb_dens;
		amb_depth = v[iz];
		
		dvnorm[volume_flux] = v[volume_flux] / 10.0;
		dvnorm[momentum_z] = momentum_z / 10.0;
		dvnorm[buoyancy_flux] = v[buoyancy_flux] / 10.0;
		dvnorm[ix] = 1.0;
		dvnorm[iz] = 1.0;
		
	} else {
		/*
		! ... Adjust gradient to correct for deviations
		 !     between ambient density as integrated along trajectory
		 !     and the correct ambient density calculated from density
		 !     profile in the current half layer
		 !     Differences arise because RK3 integrates slightly beyond
		 !     depth intervals in DEPTH array.
		 !     The adjustment will tend to give correct the error
		 !     over one half layer thickness in direction of movement
		 !     of jet (sign of D(Z,1) below),
		 !     preventing accumulation of errors.
		*/
		drho_dz = drho_dz + (amb_dens - amb_dens_i) / std::copysign(z0-z1, d[iz]);
	}
	
	int limit_exceeded=-1;
	bool neutral_point;
	int step_count;
	rk3_integrate(z0, z1, drho_dz, step_norm, s, &v[0], &vn[0], &d[0], &dvnorm[0], &accuracy[0], momentum_x, &limit_exceeded, &neutral_point, &step_count);
	
	//log_print("Limit exceeded: ", limit_exceeded, "\n");
	
	sum_steps += step_count; // TODO: this one seems unnecessary
	
	first_step = false;
	
	amb_dens_i += drho_dz*(v[iz] - amb_depth);
	amb_depth = v[iz];
	
	if(neutral_point) goto finish_and_store_out;
	
	if(limit_exceeded == 0) goto iterate_half_layer;
	
	if(limit_exceeded == 1) {
		if( (z1 <= 0.0 && v[buoyancy_flux] >= 0.0) || (z1 >= z[z.count-1] && buoyancy_flux <= 0.0) )
			goto finish_and_store_out;
		
		q_transp[layer] = (v[volume_flux] - prev_flux) * (double)n_holes;
		*transp_idx = layer;
		prev_flux = v[volume_flux];
		
		goto iterate_layer;
	}
	
finish_and_store_out :

	// Would only need anything here if we added intakes.
	
	return;
	
}