
/*
NOTE: only doodling for now.
*/

#include "../special_computations.h"


extern "C" DLLEXPORT void
nivafjord_place_river_flux(Special_Indexed_Value *target_out, Special_Indexed_Value *density_river, Special_Indexed_Value *densities_target) {
	double dens0 = density_river->at(0);
	int closest = 0;
	
	/*
	double minabs = std::numeric_limits<double>::infinity();
	for(int layer2 = 0; layer2 < densities_target->count; ++layer2) {
		double abs = std::abs(dens0 - densities_target->at(layer2));
		if(abs < minabs) {
			closest = layer2;
			minabs = abs;
		} else break;
	}
	*/
	
	for(int layer = 0; layer < densities_target->count; ++layer) {
		if(dens0 <= densities_target->at(layer)) break;
		closest = layer;
	}
	
	target_out->at(0) = (double)closest;
}

extern "C" DLLEXPORT void
nivafjord_place_horizontal_fluxes(Special_Indexed_Value *target_out, Special_Indexed_Value *densities_source, Special_Indexed_Value *densities_target, Special_Indexed_Value *widths) {
	
	// In the end, for 2-way fluxes between separate simulated basins we would need something that could take the direction of the flux also.
	
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
			} else
				break;
		}
		target_out->at(layer) = (double)closest;
	}
}

extern "C" DLLEXPORT void
nivafjord_vertical_realignment(Special_Indexed_Value *align_out, Special_Indexed_Value *horz_balance) {
	for(int layer = align_out->count-1; layer >= 0; --layer) {
		double align = horz_balance->at(layer);
		if(layer != align_out->count-1)
			align += align_out->at(layer+1);
		align_out->at(layer) = align;
	}
}

