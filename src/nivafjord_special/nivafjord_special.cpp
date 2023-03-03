
/*
NOTE: only doodling for now.
*/

#include "../special_computations.h"


extern "C" DLLEXPORT void
nivafjord_place_horizontal_fluxes(Special_Indexed_Value *target_out, Special_Indexed_Value *densities_source, Special_Indexed_Value *densities_target, Special_Indexed_Value *widths) {
	
	for(int layer = 0; layer < target_out->count; ++layer) {
		
		double dens = densities_source->at(layer);
		int closest = layer;
		
		if(widths->at(layer) == 0.0) break;
		
		double minabs = std::numeric_limits<double>::infinity();
		for(int layer2 = layer; layer2 < densities_target->count; ++layer2) {
			double abs = std::abs(dens - densities_target->at(layer2));
			if(abs < minabs) {
				closest = layer2;
				minabs = abs;
			} //else
				//break;
		}
		target_out->at(layer) = (double)closest;
	}
}