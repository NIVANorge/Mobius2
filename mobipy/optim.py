
import numpy as np
import lmfit
import emcee
from joblib import Parallel, delayed
from pyDOE import lhs
import builtins
from .plotting import chain_plot
import datetime
import pickle as pkl


# Note: we can't use multiprocessing for this, only multithreading, since a Model_Application object that is allocated from C++ on one process
# can't be accessed from a different python process.

#class Thread_Pool :
#    
#    def map(self, fn, args) :
#        return Parallel(n_jobs=-1, verbose=0, backend='threading')(builtins.map(delayed(fn), args))


def run_mcmc(app, params, set_params, log_likelihood, burn, steps, walkers, run_timeout=-1, report_interval=-1, plot_file='chains.png', result_file='result.pkl', progress=False) :

	def ll_fun(params) :
		
		data = app.copy()
		set_params(data, params)
		success = data.run(run_timeout)
		if success :
			ll = log_likelihood(data, params)
		else :
			ll = -np.inf
		del data
		
		return ll
	
	init_values = np.array([params[par_name].value for par_name in params])
	mins = np.array([params[par_name].min for par_name in params])
	maxs = np.array([params[par_name].max for par_name in params])
	
	starting_guesses = list(np.random.normal(loc=init_values, scale=1e-4*(maxs-mins), size=(walkers, len(init_values))))
	starting_guesses = np.maximum(starting_guesses, mins)
	starting_guesses = np.minimum(starting_guesses, maxs)
	
	mcmc = lmfit.Minimizer(ll_fun, params, nan_policy='omit', kws={'moves':emcee.moves.StretchMove(), 'skip_initial_state_check':True})
	
	if report_interval < 0 :
		return mcmc.emcee(params=params, pos=starting_guesses, burn=burn, steps=steps, nwalkers=walkers, float_behavior='posterior', progress=progress)
		#Threading doesn't work, no idea why.
		# It works to run this with a non-Mobius ll function, and it also works to run a Mobius2 model separately in a Thread_Pool, but the combination causes very
		# low acceptance rate in the chains it looks like..
		#pool = Thread_Pool()
		#return mcmc.emcee(params=params, pos=starting_guesses, burn=burn, steps=steps, nwalkers=walkers, workers=pool, float_behavior='posterior') 
	else :
		steps_left = steps
		use_steps = min(steps_left, report_interval)
		reuse_sampler = False #Initially it must create a new one.
		while steps_left > 0 :
			ts = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
			print('%s : Running MCMC, %d steps left.' % (ts, steps_left))
			run_mcmc_kwargs={'skip_initial_state_check':True} # Otherwise it crashes if the chains become too linearly dependent during the run.
			# Note: can't use the burn parameter here since it maybe higher than the partial runs. But it doesn't matter for the outcome of the run.
			result = mcmc.emcee(
				params=params, 
				pos=starting_guesses, 
				burn=0, 
				steps=use_steps, 
				nwalkers=walkers, 
				float_behavior='posterior', 
				progress = False,   #Doesn't make much sense in this use case since there is an outer progress report?
				reuse_sampler=reuse_sampler, 
				run_mcmc_kwargs=run_mcmc_kwargs)
			reuse_sampler=True
			steps_left -= use_steps
			use_steps = min(steps_left, report_interval)
			if plot_file :
				chain_plot(result, filename=plot_file)
			if result_file :
				with open(result_file,'wb') as resfile :
					pkl.dump({'result' : result}, resfile)
					
		return result
			
	
def update_mcmc_results(result, nburn, thin=1):
    """ The summary statistics contained in the LMFit result object do not account for
        the burn-in period. This function discards the burn-in, thins and then re-calculates
        parameter medians and standard errors. The 'user_data' attribute for each parameter
        is then updated to include two new values: 'median' and 'map'. These can be passed to
        set_parameter_values() via the 'use_stat' kwarg.        
        
    Args:
        result: Obj. LMFit result object with method='emcee'
        nburn:  Int. Number of steps to discrad from the start of each chain as 'burn-in'
        thin:   Int. Keep only every 'thin' steps
        
    Returns:
        Updated LMFit result object.    
    """
    # Discard the burn samples and thin
    chain = result.chain[nburn::thin, :, :]
    ndim = result.chain.shape[-1]
	
    flatchain = chain.reshape((-1, ndim))

    # 1-sigma quantile, estimated as half the difference between the 15 and 84 percentiles
    quantiles = np.percentile(flatchain, [15.87, 50, 84.13], axis=0)

    for i, var_name in enumerate(result.var_names):
        std_l, median, std_u = quantiles[:, i]
        result.params[var_name].value = median
        result.params[var_name].stderr = 0.5 * (std_u - std_l)
        result.params[var_name].correl = {}

    result.params.update_constraints()

    # Work out correlation coefficients
    corrcoefs = np.corrcoef(flatchain.T)

    for i, var_name in enumerate(result.var_names):
        for j, var_name2 in enumerate(result.var_names):
            if i != j:
                result.params[var_name].correl[var_name2] = corrcoefs[i, j]

    # Add both the median and the MAP as additonal 'user_data' pars in the 'result' object
    lnprob = result.lnprob[nburn::thin, :]
    highest_prob = np.argmax(lnprob)
    hp_loc = np.unravel_index(highest_prob, lnprob.shape)
    map_soln = chain[hp_loc]
    
    for i, var_name in enumerate(result.var_names):
        std_l, median, std_u = quantiles[:, i]
        result.params[var_name].user_data['median'] = median
        result.params[var_name].user_data['map'] = map_soln[i]
        
    return result 



def ll_wls(sim, obs, params, err_sym=None) :
	l2pi = 1.83787706641 #np.log(2*np.pi)
	
	muname = '__mu'
	siname = '__sigma'
	if err_sym :
		muname = '__mu_%s' % err_sym
		siname = '__sigma_%s' % err_sym
	
	mu = params[muname].value
	sig = params[siname].value
	st  = mu + sig*sim
	
	vals = 0.5*(-np.log(st**2) - l2pi - ((sim-obs)**2)/(st**2) )
	return np.nansum(vals)


def residual_from_target(target, start_date, end_date, normalize=False) :
	# This is only for use with the least squares minimizer, which takes the entire residual vector
	
	sl = slice(start_date, end_date)
	
	def single_residual(data, target) :
		sim = data.var(target[0])[target[1]].loc[sl].values
		obs = data.var(target[2])[target[3]].loc[sl].values
		
		weight = 1 if len(target)==4 else target[4]
		n_valid = 1 if not normalize else np.sum(~np.isnan(obs))
		
		return sim*np.sqrt(weight/n_valid), obs*np.sqrt(weight/n_valid)
	
	if isinstance(target, list) :
		
		def get_sim_obs(data) :
			sim = []
			obs = []
			for tar in target :
				sm, ob = single_residual(data, tar)
				sim.append(sm)
				obs.append(ob)
			return np.concatenate(sim), np.concatenate(obs)
		
		return get_sim_obs
		
	else :
		def get_sim_obs(data) :
			return single_residual(data, target)
			
		return get_sim_obs
	

def ll_from_target(target, start_date, end_date, ll_fun=ll_wls, err_syms=None) :
	
	sl = slice(start_date, end_date)
	
	if isinstance(target, list) :
		
		def log_likelihood(data, params, n_run=None) :
			
			sum = 0.0
			for tar in target :
				simname, simidx, obsname, obsidx, wgt = tar
				sim = data.var(simname)[simidx].loc[sl].values
				obs = data.var(obsname)[obsidx].loc[sl].values
				
				err_sym = None
				if err_syms :
					err_sym = err_syms[obsname]
				
				sum += ll_fun(sim, obs, params, err_sym)*wgt
			return sum
		
		return log_likelihood
	
	else :
		
		def log_likelihood(data, params, n_run=None) :
			
			simname, simidx, obsname, obsidx = target
			sim = data.var(simname)[simidx].loc[sl].values
			obs = data.var(obsname)[obsidx].loc[sl].values
			
			err_sym = None
			if err_syms :
				err_sym = err_syms[obsname]
			
			res = ll_fun(sim, obs, params, err_sym)
			
			return res
		
		return log_likelihood
	
def params_from_dict(app, dict) :
	params = lmfit.Parameters()
	
	for par_name in dict :
		module, ident, indexes, mn, mx, = dict[par_name]   #TODO: Also allow expr?
		
		val = app[module].__getattr__(ident)[indexes]
		
		params.add(name=par_name, value=val, min=mn, max=mx)
		params[par_name].user_data = {}
		params[par_name].user_data['Mobius_id'] = (module, ident, indexes)
		
	def set_params(data, params) :
		for par_name in params :
			if par_name.startswith('__') : continue
			
			par = params[par_name]
			module, ident, indexes = par.user_data['Mobius_id']
			data[module].__getattr__(ident)[indexes] = par.value
		
	return params, set_params

# Hmm, not that transferable to multitarget...
def add_wls_params(params, muinit, mumin, mumax, siminit, simin, simax, sym=None) :
	muname = '__mu'
	signame = '__sigma'
	if sym :
		muname = '__mu_%s'%sym
		signame = '__sigma_%s'%sym
	params.add(name=muname, min=mumin, max=mumax, value=muinit)
	params.add(name=signame, min=simin, max=simax, value=siminit)
	params[muname].user_data = {}
	params[signame].user_data = {}

def run_latin_hypercube_sample(app, params, set_params, target_stat, n_samples, run_timeout=-1, verbose=1) :
	
	# Draw the latin hypercube sample of parameters
	par_data = lhs(len(params), samples=n_samples)
    
	# Re-scale to correct (min, max) interval
	for i, par_name in enumerate(params) :
		par = params[par_name]
		par_data[:, i] = par.min + (par.max - par.min)*par_data[:, i]
	
	def sample_fun(n_run) :
		
		pars = params.copy()
		for i, par_name in enumerate(pars) :
			pars[par_name].value = par_data[n_run, i]
		
		data = app.copy()
		set_params(data, pars)
		success = data.run(run_timeout)
		if success :
			stat = target_stat(data, pars, n_run)
		else :
			stat = -np.inf
		del data
		
		return stat
	
	stats = Parallel(n_jobs=-1, verbose=verbose, backend='threading')(map(delayed(sample_fun), range(n_samples)))
	
	return par_data, stats
	
def run_minimizer(app, params, set_params, residual_fun, method='nelder', use_init=True, run_timeout=-1, disp=False) :
	
	def get_residuals(pars) :
		
		data = app.copy()
		set_params(data, pars)
		success = data.run(run_timeout)
		if success :
			sim, obs = residual_fun(data)
			resid = sim - obs
		else :
			resid = np.array([np.inf])
		del data
		
		# There is currently a bug in lmfit where it can't find the correct result if there is only one valid residual!
		# So we have to fake add another one.
		if sum(~np.isnan(resid)) < 2 :
			resid = np.append(resid, 0)
			
		
		return resid
	
	mi = lmfit.Minimizer(get_residuals, params, nan_policy='omit')

	options = {'disp' : disp}
    
	init = None
	if use_init : init = params
	
	if method == 'leastsq' :
		res = mi.minimize(method=method, params=init)
	else :
		res = mi.minimize(method=method, params=init, options=options)
	
	set_params(app, res.params)
	
	return res
	
def are_params_near_bounds(params, reltol=0.01, upper_only=True) :
	
	which = []
	for name in params :
		par = params[name]
		tol = (par.max - par.min)*reltol
		if np.abs(par.max - par.value)<=tol or ((not upper_only) and (np.abs(par.min - par.value)<=tol)) :
			which.append(par.name)
			
	return which