
#include "optimization.h"

int
set_parameters(Model_Data *data, const std::vector<Indexed_Parameter> &parameters, double *values, bool use_expr) {
	Parameter_Value val;
	if(!use_expr) {
		int par_idx = 0;
		for(const auto &par : parameters) {
			val.val_real = values[par_idx++];
			if(!par.virt)
				set_parameter_value(par, data, val);
		}
		return -1;
	}
	
	//TODO
	
	return -1;
}