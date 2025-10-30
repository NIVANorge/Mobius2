
from .. import optim as opt

def get_nh4_setup(app, calib_index='Soil', obs_index='River', obsname='Obs NH4', start=None, end=None) :
	
	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'nitr' : ('MAGIC-Forest CNP', 'nitr', [calib_index], -100, 0),
	}
	
	target = ('NH4(+) ionic concentration', [obs_index], obsname, [])
	
	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end)

	return params, set_params, target, get_sim_obs

def get_no3_setup(app, calib_index='Soil', obs_index='River', obsname='Obs NO3', start=None, end=None) :
	
	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'denitr' : ('MAGIC-Forest CNP', 'denitr', [calib_index], -100, 0),
	}
	
	target = ('NO3(-) ionic concentration', [obs_index], obsname, [])

	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end)

	return params, set_params, target, get_sim_obs
	
def get_ph_setup(app, calib_index='Soil', obsname='Obs Ph', do_al=False, obsal='Observed LAL', start=None, end=None) :
	
	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'oa' : ('MAGIC-Forest drivers', 'oa', [calib_index], 0, 200),
	}
	if do_al :
		par_dict['k_al'] = ('MAGIC core', 'k_al', [calib_index], 1, 20)
	
	target = [('pH', [calib_index], obsname, [])]
	if do_al :
		target.append(('Total aluminum in solution (ionic + SO4-F-DOC complexes)', [calib_index], obsal, []))
	
	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end)

	return params, set_params, target, get_sim_obs
	
def get_base_cation_weathering_setup(app, calib_index='Soil', obs_index='River', obsname='Obs %s', start=None, end=None) :
	
	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'w_ca' : ('MAGIC-Forest drivers', 'w_ca', [calib_index], 0, 200),
		'w_mg' : ('MAGIC-Forest drivers', 'w_mg', [calib_index], 0, 200),
		'w_na' : ('MAGIC-Forest drivers', 'w_na', [calib_index], 0, 200),
		'w_k' : ('MAGIC-Forest drivers', 'w_k', [calib_index], 0, 200),
	}
	
	target = [
		('Ca(2+) ionic concentration', [obs_index], obsname%'Ca', [], 1),
		('Mg(2+) ionic concentration', [obs_index], obsname%'Mg', [], 1),
		('Na(+) ionic concentration', [obs_index], obsname%'Na', [], 1),
		('K(+) ionic concentration', [obs_index], obsname%'K', [], 1),
	]
	
	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end)

	return params, set_params, target, get_sim_obs
	
def get_base_cation_exchangeable_fractions_setup(app, calib_index='Soil', obsname='Obs %s%%', start=None, end=None) :
	
	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'init_eca' : ('MAGIC core', 'init_eca', [calib_index], 0, 50),
		'init_emg' : ('MAGIC core', 'init_emg', [calib_index], 0, 20),
		'init_ena' : ('MAGIC core', 'init_ena', [calib_index], 0, 20),
		'init_ek' : ('MAGIC core', 'init_ek', [calib_index], 0, 20),
	}
	
	target = [
		('Exchangeable Ca on soil as % of CEC', [calib_index], obsname%'Ca', [], 1),
		('Exchangeable Mg on soil as % of CEC', [calib_index], obsname%'Mg', [], 1),
		('Exchangeable Na on soil as % of CEC', [calib_index], obsname%'Na', [], 1),
		('Exchangeable K on soil as % of CEC', [calib_index], obsname%'K', [], 1),
	]
	
	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end)

	return params, set_params, target, get_sim_obs
	
def get_base_cation_combined_setup(app, calib_index='Soil', obs_index='River', obsname_c='Obs %s', obsname_bs='Obs%s%%', obsname_ph=None, do_al = False, bs_weight=10, start=None, end=None) :

	if start is None : start = app.start_date[()]
	if end is None : end = app.end_date[()]
	
	par_dict = {
		'w_ca' : ('MAGIC-Forest drivers', 'w_ca', [calib_index], 0, 200),
		'w_mg' : ('MAGIC-Forest drivers', 'w_mg', [calib_index], 0, 200),
		'w_na' : ('MAGIC-Forest drivers', 'w_na', [calib_index], 0, 200),
		'w_k' : ('MAGIC-Forest drivers', 'w_k', [calib_index], 0, 200),
		'init_eca' : ('MAGIC core', 'init_eca', [calib_index], 0, 50),
		'init_emg' : ('MAGIC core', 'init_emg', [calib_index], 0, 20),
		'init_ena' : ('MAGIC core', 'init_ena', [calib_index], 0, 20),
		'init_ek' : ('MAGIC core', 'init_ek', [calib_index], 0, 20),
	}
	#if obsname_ph :
	par_dict['oa'] = ('MAGIC-Forest drivers', 'oa', [calib_index], 40, 100)
	
	if do_al :
		par_dict['k_al'] = ('MAGIC core', 'k_al', [calib_index], 1, 20)
	
	target = [
		('Ca(2+) ionic concentration', [obs_index], obsname_c%'Ca', [], 1),
		('Mg(2+) ionic concentration', [obs_index], obsname_c%'Mg', [], 1),
		('Na(+) ionic concentration', [obs_index], obsname_c%'Na', [], 1),
		('K(+) ionic concentration', [obs_index], obsname_c%'K', [], 1),
		('Exchangeable Ca on soil as % of CEC', [calib_index], obsname_bs%'Ca', [], bs_weight),
		('Exchangeable Mg on soil as % of CEC', [calib_index], obsname_bs%'Mg', [], bs_weight),
		('Exchangeable Na on soil as % of CEC', [calib_index], obsname_bs%'Na', [], bs_weight),
		('Exchangeable K on soil as % of CEC', [calib_index], obsname_bs%'K', [], bs_weight),
	]
	if obsname_ph :
		target.append(('pH', [calib_index], obsname_ph, [], 500))
	
	params, set_params = opt.params_from_dict(app, par_dict)

	get_sim_obs = opt.residual_from_target(target, start, end, normalize=True)
	#get_sim_obs = opt.residual_from_target(target, app.start_date[()], app.end_date[()], normalize=True)

	return params, set_params, target, get_sim_obs