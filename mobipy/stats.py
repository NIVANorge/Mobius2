import numpy as np


def pearson(sim0, obs, align_sim=True) :

	if align_sim :
		sim = sim0.copy()
		sim[np.isnan(obs)] = np.nan
	else :
		sim = sim0
		
	msim = np.nanmean(sim)
	mobs = np.nanmean(obs)
	
	return np.nansum((sim - msim) * (obs - mobs)) / np.sqrt(np.nansum((sim - msim)**2)*np.nansum((obs - mobs)**2))
	
def r2(sim, obs) :

	return pearson(sim, obs)**2

def nash_sutcliffe(sim, obs) :
	
	return 1 - (np.nansum((sim - obs)**2) / np.nansum((np.nanmean(obs) - obs)**2))

def kling_gupta(sim0, obs) :
	
	sim = sim0.copy()
	sim[np.isnan(obs)] = np.nan

	r = pearson(sim, obs, False)
	
	alpha = np.nanstd(sim) / np.nanstd(obs)
    
	beta = np.nansum(sim) / np.nansum(obs)
	
	return 1 - np.sqrt((r - 1)**2 + (alpha - 1)**2 + (beta - 1)**2)
	
def weighted_quantile(values, quantiles, sample_weight=None, values_sorted=False, old_style=False):	
	# taken from
	# https://stackoverflow.com/questions/21844024/weighted-percentile-using-numpy
	""" Very close to numpy.percentile, but supports weights.
	NOTE: quantiles should be in [0, 1]!
	:param values: numpy.array with data
	:param quantiles: array-like with many quantiles needed
	:param sample_weight: array-like of the same length as `array`
	:param values_sorted: bool, if True, then will avoid sorting of
		initial array
	:param old_style: if True, will correct output to be consistent
		with numpy.percentile.
	:return: numpy.array with computed quantiles.
	"""
	values = np.array(values)
	quantiles = np.array(quantiles)
	if sample_weight is None:
		sample_weight = np.ones(len(values))
	sample_weight = np.array(sample_weight)
	assert np.all(quantiles >= 0) and np.all(quantiles <= 1), 'quantiles should be in [0, 1]'

	if not values_sorted:
		sorter = np.argsort(values)
		values = values[sorter]
		sample_weight = sample_weight[sorter]

	weighted_quantiles = np.cumsum(sample_weight) - 0.5 * sample_weight
	if old_style:
		# To be convenient with numpy.percentile
		weighted_quantiles -= weighted_quantiles[0]
		weighted_quantiles /= weighted_quantiles[-1]
	else:
		weighted_quantiles /= np.sum(sample_weight)
	return np.interp(quantiles, weighted_quantiles, values)