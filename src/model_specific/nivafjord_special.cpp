
#include "../external_computations.h"


extern "C" DLLEXPORT void
nivafjord_place_river_flux(Value_Access *values) {
	auto &target_out = values[0];
	auto &density_river = values[1];
	auto &max_layer = values[2];
	auto &densities_target = values[3];
	
	double dens0 = density_river.at(0);
	int closest = 0;
	
	int maxd = std::min((int)max_layer.at(0), (int)densities_target.count);
	
	for(int layer = 0; layer < maxd; ++layer) {
		if(dens0 <= densities_target.at(layer)) break;
		closest = layer;
	}
	
	target_out.at(0) = (double)closest;
}

extern "C" DLLEXPORT void
nivafjord_place_horizontal_fluxes(Value_Access *values) {
	auto &target_out = values[0];
	auto &densities1 = values[1];
	auto &densities2 = values[2];
	auto &pressure1  = values[3];
	auto &pressure2  = values[4];
	auto &widths     = values[5];
	
	for(int layer = 0; layer < target_out.count; ++layer) {
		
		if(widths.at(layer) == 0.0) break;
		
		double dP = pressure1.at(layer) - pressure2.at(layer);
		int closest = layer;
		double minabs = std::numeric_limits<double>::infinity();
		
		if(dP > 0) {
			double dens = densities1.at(layer);
			for(int layer2 = layer; layer2 < densities2.count; ++layer2) {
				double abs = std::abs(dens - densities2.at(layer2));
				if(abs < minabs) {
					closest = layer2;
					minabs = abs;
				} else
					break;
			}
			target_out.at(layer) = (double)closest;
		} else {
			double dens = densities2.at(layer);
			for(int layer2 = layer; layer2 < densities1.count; ++layer2) {
				double abs = std::abs(dens - densities1.at(layer2));
				if(abs < minabs) {
					closest = layer2;
					minabs = abs;
				} else
					break;
			}
			target_out.at(closest) = (double)layer; // This instead gives the level of the other layer that flows to this one.
		}
	}
}

extern "C" DLLEXPORT void
nivafjord_compute_pressure_difference(Value_Access *values) {
	auto &dPout = values[0];
	auto &z0 = values[1];
	auto &z1 = values[2];
	auto &dz0 = values[3];
	auto &dz1 = values[4];
	auto &P0 = values[5];
	auto &P1 = values[6];
	
	// Compute the pressure difference between two horizontally neighbouring layers.

	int other_layer = 0;
	double zbot_other;
	
	for(int layer = 0; layer < dPout.count; ++layer) {
		double Ptop = 0.0;
		double Pbot = P0.at(layer);
		if(layer > 0) Ptop = P0.at(layer-1);
		double P = 0.5*(Ptop + Pbot);  // mean pressure in layer.
		
		double zbot = z0.at(layer);
		double ztop = zbot - dz0.at(layer);
		
		double z = 0.5*(zbot + ztop);
		
		for(; other_layer < dPout.count; ++other_layer) {
			zbot_other = z1.at(other_layer);
			if(zbot_other >= z) break;
		}
	
		double t = (zbot_other - z) / dz1.at(other_layer);
		t = std::min(std::max(t, 0.0), 1.0);
		
		double Ptop_other = 0.0;
		if(other_layer > 0) Ptop_other = P1.at(other_layer-1);
		double Pbot_other = P1.at(other_layer);
		
		double Palign = (1.0 - t)*Pbot_other + t*Ptop_other;
		
		dPout.at(layer) = P - Palign;
	}
	
}

extern "C" DLLEXPORT void
nivafjord_vertical_realignment(Value_Access *values) {
	auto &align_out = values[0];
	auto &horz_balance = values[1];
	// Distribute so that the net water balance is 0 in every layer except the top layer
	// NOTE: align_out.at(i) gives the flow from layer i to layer i+1
	
	align_out.at(align_out.count-1) = 0.0;
	double align_below = 0.0;
	for(int layer = align_out.count-2; layer >= 0; --layer)
		align_below = (align_out.at(layer) = horz_balance.at(layer + 1) + align_below);
}

extern "C" DLLEXPORT void
nivafjord_vertical_realignment_even_distrib(Value_Access *values) {
	// Distribute so that the net balance is distributed area-weighted. (I.e. keep layer thickness evenly distributed).
	auto &align_out = values[0];
	auto &horz_balance = values[1];
	auto &area = values[2];
	auto &surf_bal = values[3];
	
	double sum_bal = 0.0;
	double sum_area = 0.0;
	for(int layer = 0; layer < horz_balance.count; ++layer) {
		sum_bal += horz_balance.at(layer);
		sum_area += area.at(layer);
	}
	// Have to take surface fluxes into account in the total water balance.
	sum_bal += surf_bal.at(0);
	
	align_out.at(align_out.count-1) = 0.0;
	double align_below = 0.0;
	for(int layer = align_out.count-2; layer >= 0; --layer) {
		double want_bal = sum_bal*area.at(layer+1)/sum_area;
		align_below = (align_out.at(layer) = (horz_balance.at(layer+1) + align_below - want_bal));
	}
}

extern "C" DLLEXPORT void
nivafjord_vertical_realignment_slow(Value_Access *values) {
	// Distribute so that the thickness tends towards dz0, but delayed to not cause too high realignment fluxes.
	auto &align_out = values[0];
	auto &dz = values[1];
	auto &dz0 = values[2];
	auto &area = values[3];
	auto &rate = values[4];
	
	double rate0 = rate.at(0);
	
	align_out.at(align_out.count-1) = 0.0;
	double align_below = 0.0;
	for(int layer = align_out.count-2; layer >= 0; --layer) {
		
		double want_bal = area.at(layer+1)*(dz.at(layer+1) - dz0.at(layer+1))*rate0;
		align_out.at(layer) = align_below-want_bal;
		
		align_below = align_out.at(layer);
	}
	
}
