
import numpy as np
import lmfit
import emcee
from joblib import Parallel, delayed
from pyDOE import lhs


# Note: we can't use multiprocessing for this, only multithreading, since a Model_Application object that is allocated from C++ one one process
# can't be accessed from a different python process.
class Thread_Pool :
    
    def map(self, fn, args) :
        return Parallel(n_jobs=-1, verbose=0, backend='threading')(map(delayed(fn), args))


def run_mcmc(app, params, set_params, log_likelihood, burn, steps, walkers, run_timeout=-1) :

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
	
	mcmc = lmfit.Minimizer(ll_fun, params, nan_policy='omit', kws={'moves':emcee.moves.StretchMove()})

	return mcmc.emcee(params=params, burn=burn, steps=steps, nwalkers=walkers, workers=Thread_Pool(), float_behavior='posterior')
	
	
def ll_wls(sim, obs, params) :
	l2pi = 1.83787706641 #np.log(np.pi)
	
	mu  = params["__mu"].value
	sig = params["__sigma"].value
	st  = mu + sig*obs
	
	vals = 0.5*(-np.log(st**2) - l2pi - ((sim-obs)**2)/(st**2) )
	return np.nansum(vals)


# TODO: Take target list
def ll_from_target(target, start_date, end_date, ll_fun=ll_wls) :

	simname, simidx, obsname, obsidx = target
	sl = slice(start_date, end_date)
	
	def log_likelihood(data, params) :
		
		sim = data.var(simname)[simidx].loc[sl].values
		obs = data.var(obsname)[obsidx].loc[sl].values
		
		return ll_fun(sim, obs, params)
		
	
	return log_likelihood
	
def params_from_dict(app, dict) :
	params = lmfit.Parameters()
	
	for par_name in dict :
		module, ident, indexes, mn, mx, = dict[par_name]   #TODO: Also allow expr?
		
		val = app[module].__getattr__(ident)[indexes]
		
		params.add(name=par_name, value=val, min=mn, max=mx)
		params[par_name].user_data = (module, ident, indexes)
		
	def set_params(data, params) :
		for par_name in params :
			if par_name.startswith('__') : continue
			
			par = params[par_name]
			module, ident, indexes = par.user_data
			data[module].__getattr__(ident)[indexes] = par.value
		
	return params, set_params

# Hmm, not that transferable to multitarget...
def add_wls_params(params, muinit, mumin, mumax, siminit, simin, simax) :
	params.add(name='__mu', min=mumin, max=mumax, value=muinit)
	params.add(name='__sigma', min=simin, max=simax, value=siminit)


def latin_hypercube_sample(app, params, n_samples, set_params, target_stat, run_timeout=-1, verbose=1) :
	
	# Draw the latin hypercube sample of parameters
	par_data = lhs(len(params), samples=n_samples)
    
	# Re-scale to correct (min, max) interval
	for i, par_name in enumerate(params) :
		par = params[par_name]
		par_data[:, i] = par.min + (par.max - par.min)*par_data[:, i]
	
	def sample_fun(n_run) :
		
		pars = params.copy()
		for i, par_name in enumerate(params) :
			pars[par_name].value = par_data[n_run, i]
		
		data = app.copy()
		set_params(data, pars)
		success = data.run(run_timeout)
		if success :
			stat = target_stat(data, pars)
		else :
			stat = -np.inf
		del data
		
		return stat
	
	stats = Parallel(n_jobs=-1, verbose=verbose, backend="threading")(map(delayed(sample_fun), range(n_runs)))
	
	return par_data, stats