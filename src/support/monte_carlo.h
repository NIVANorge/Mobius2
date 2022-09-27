
struct
MC_Data {
	
	void allocate(s64 n_walkers, s64 n_pars, s64 n_steps);
	
	void extend_steps(s64 n_steps_new);
	
	/*
		IMPORTANT NOTICE: It is important for usage purposes that the steps of one parameter for one walker are
		stored sequentially. E.g. when increasing the step by one, but keeping the parameter
		and walker constant, the address should only change by one entry. This allows for easy
		plotting of the chains.
		
		//TODO: fix this by making a DataSource for the plot instead
	*/
	
	double &operator()(int walker, int par, int step) {
		return par_data[step + n_steps*(par + n_pars*walker)];
	}
	
	double &score_value(int walker, int step) {
		return ll_data[walker*n_steps + step];
	}
	
	void get_map_index(int burnin, int cur_step, int &best_walker, int &best_step);
	
	void free_data() {
		if(par_data) free(par_data);
		if(ll_data)  free(ll_data);
		par_data = nullptr;
		ll_data  = nullptr;
	}

private :
	double *par_data = nullptr;
	double *ll_data  = nullptr;
	s64 n_steps = 0, n_walkers = 0, n_pars = 0;
	s64 n_accepted = 0;
};
