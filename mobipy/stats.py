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