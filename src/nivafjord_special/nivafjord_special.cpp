
/*
NOTE: only doodling for now.
*/


#include "../common_types.h"
#include "nivafjord_special.h"


extern "C" DLLEXPORT void
place_horizontal_fluxes(Special_Indexed_Value target_out, Special_Indexed_Value n_layers, Special_Indexed_Value densities0, Special_Indexed_Value densities1) {
	
	//TODO: have to extract n_layers...
	
	for(int layer = 0; layer < n_layers; ++layer) {
		double dens = densities0[layer];
		int closest = layer;
		
		double minabs = std::numeric_limits<double>::infinity();
		for(int layer2 = layer; layer2 < n_layers; ++layer) {
			double abs = std::abs(dens - densities1[layer2]);
			if(abs < minabs) {
				closest = layer2;
				minabs = abs;
			}
		}
		target_out[layer] = (double)closest;
	}
}