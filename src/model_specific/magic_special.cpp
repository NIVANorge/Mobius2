
#include "../external_computations.h"
#include "MAGIC_core.h"

extern "C" DLLEXPORT void
magic_core(Value_Access *values) {
	
	auto &out_conc_ca = values[0];
	auto &out_conc_mg = values[1];
	auto &out_conc_na = values[2];
	auto &out_conc_k = values[3];
	auto &out_conc_nh4 = values[4];
	auto &out_conc_so4 = values[5];
	auto &out_conc_cl = values[6];
	auto &out_conc_no3 = values[7];
	auto &out_conc_f = values[8];
	auto &out_conc_po4 = values[9];
	auto &out_conc_all_so4 = values[10];
	auto &out_conc_all_f = values[11];
	auto &out_ph = values[12];
	auto &out_exch_ca = values[13];
	auto &out_exch_mg = values[14];
	auto &out_exch_na = values[15];
	auto &out_exch_k = values[16];
	auto &out_exch_so4 = values[17];
	auto &out_bss = values[18];
	auto &out_conc_hco3 = values[19];
	auto &out_conc_co3 = values[20];
	auto &out_all_doc = values[21];
	auto &out_all_dic = values[22];
	auto &out_conc_al = values[23];
	auto &out_all_al = values[24];
	auto &out_org_al = values[25];
	auto &io_conc_h = values[26];
	auto &io_ionic_strength = values[27];
	auto &in_total_ca = values[28];
	auto &in_total_mg = values[29];
	auto &in_total_na = values[30];
	auto &in_total_k = values[31];
	auto &in_total_nh4 = values[32];
	auto &in_total_so4 = values[33];
	auto &in_total_cl = values[34];
	auto &in_total_no3 = values[35];
	auto &in_total_f = values[36];
	auto &in_total_po4 = values[37];
	auto &par_max_diff = values[38];
	auto &par_is_soil = values[39];
	auto &par_depth = values[40];
	auto &par_temperature = values[41];
	auto &par_pco2 = values[42];
	auto &par_conc_doc = values[43];
	auto &par_log10_aloh3_equilibrium_const = values[44];
	auto &par_h_aloh3_exponent = values[45];
	auto &par_pk1doc = values[46];
	auto &par_pk2doc = values[47];
	auto &par_pk3doc = values[48];
	auto &par_pk1aldoc = values[49];
	auto &par_pk2aldoc = values[50];
	auto &par_porosity = values[51];
	auto &par_bulk_density = values[52];
	auto &par_cec = values[53];
	auto &par_so4_half_sat = values[54];
	auto &par_so4_max_cap = values[55];
	auto &par_log10_ca_al = values[56];
	auto &par_log10_mg_al = values[57];
	auto &par_log10_na_al = values[58];
	auto &par_log10_k_al = values[59];

	magic_input input = {};
	magic_param param = {};
	magic_output result = {};
	
	input.total_Ca  = in_total_ca.at(0);
	input.total_Mg  = in_total_mg.at(0);
	input.total_Na  = in_total_na.at(0);
	input.total_K   = in_total_k.at(0);
	input.total_NH4 = in_total_nh4.at(0);
	input.total_SO4 = in_total_so4.at(0);
	input.total_Cl  = in_total_cl.at(0);
	input.total_NO3 = in_total_no3.at(0);
	input.total_F   = in_total_f.at(0);
	input.total_PO4 = in_total_po4.at(0);
	
	param.Depth              = par_depth.at(0);
	param.Temperature        = par_temperature.at(0);
	param.PartialPressureCO2 = par_pco2.at(0);
	param.conc_DOC           = par_conc_doc.at(0);
	param.Log10AlOH3EquilibriumConst = par_log10_aloh3_equilibrium_const.at(0);
	param.HAlOH3Exponent     = par_h_aloh3_exponent.at(0);
	param.pK1DOC             = par_pk1doc.at(0);
	param.pK2DOC             = par_pk2doc.at(0);
	param.pK3DOC             = par_pk3doc.at(0);
	param.pK1AlDOC           = par_pk1aldoc.at(0);
	param.pK2AlDOC           = par_pk2aldoc.at(0);
	param.Porosity           = par_porosity.at(0);
	param.BulkDensity        = par_bulk_density.at(0);
	param.CationExchangeCapacity = par_cec.at(0);
	param.SO4HalfSat         = par_so4_half_sat.at(0);
	param.SO4MaxCap          = par_so4_max_cap.at(0);
	param.Log10CaAlSelectCoeff = par_log10_ca_al.at(0);
	param.Log10MgAlSelectCoeff = par_log10_mg_al.at(0);
	param.Log10NaAlSelectCoeff = par_log10_na_al.at(0);
	param.Log10KAlSelectCoeff  = par_log10_k_al.at(0);
	
	// NOTE: time_step and comp_index were only used in an error message, but we had to disable that for now since we can't throw errors from model code anymore.
	s64 time_step = 0;
	s64 comp_index = 0;
	
	bool is_soil = par_is_soil.bool_at(0);
	double conv = par_max_diff.at(0);
	
	double h_est = io_conc_h.at(0);
	double ionic_strength = io_ionic_strength.at(0);
	
	MagicCore(input, param, result, is_soil, conv, h_est, ionic_strength, time_step, comp_index);
	
	// NOTE: Converting mmol to meq
	out_conc_ca.at(0) = 2.0*result.conc_Ca;
	out_conc_mg.at(0) = 2.0*result.conc_Mg;
	out_conc_na.at(0) = result.conc_Na;
	out_conc_k.at(0) = result.conc_K;
	out_conc_nh4.at(0) = result.conc_NH4;
	out_conc_so4.at(0) = 2.0*result.conc_SO4;
	out_conc_cl.at(0) = result.conc_Cl;
	out_conc_no3.at(0) = result.conc_NO3;
	out_conc_f.at(0) = result.conc_F;
	out_conc_po4.at(0) = 3.0*result.conc_PO4;
	
	out_conc_all_so4.at(0) = result.all_SO4 * 2.0;
	out_conc_all_f.at(0) = result.all_F;
	
	out_ph.at(0) = result.pH;
	
	// NOTE: Converting fraction to percent
	out_exch_ca.at(0)  = 100.0*result.exchangeable_Ca;
	out_exch_mg.at(0)  = 100.0*result.exchangeable_Mg;
	out_exch_na.at(0)  = 100.0*result.exchangeable_Na;
	out_exch_k.at(0)   = 100.0*result.exchangeable_K;
	out_exch_so4.at(0) = 100.0*result.exchangeable_SO4;
	out_bss.at(0)      = 100.0*result.BaseSaturationSoil;
	
	out_conc_hco3.at(0) = result.conc_HCO3,
	out_conc_co3.at(0)  = result.conc_CO3,
	
	out_all_doc.at(0) = result.all_DOC,
	out_all_dic.at(0) = result.all_DIC,
	
	out_conc_al.at(0) = result.conc_Al;
	out_all_al.at(0) = result.all_Al;
	out_org_al.at(0) = result.org_Al;
	
	io_conc_h.at(0) = result.conc_H;
	io_ionic_strength.at(0) = result.IonicStrength;
}


extern "C" DLLEXPORT void
magic_core_initial(Value_Access *values) {
	auto &out_conc_SO4 = values[0];
	auto &out_conc_F = values[1];
	auto &out_conc_H = values[2];
	auto &out_log10_ca_al = values[3];
	auto &out_log10_mg_al = values[4];
	auto &out_log10_na_al = values[5];
	auto &out_log10_k_al = values[6];
	auto &out_ph = values[7];
	auto &out_ionic_str = values[8];
	auto &out_exch_so4 = values[9];
	auto &in_conc_Ca = values[10];
	auto &in_conc_Mg = values[11];
	auto &in_conc_Na = values[12];
	auto &in_conc_K = values[13];
	auto &in_conc_NH4 = values[14];
	auto &in_all_SO4 = values[15];
	auto &in_conc_Cl = values[16];
	auto &in_conc_NO3 = values[17];
	auto &in_all_F = values[18];
	auto &in_conc_PO4 = values[19];
	auto &in_exch_Ca = values[20];
	auto &in_exch_Mg = values[21];
	auto &in_exch_Na = values[22];
	auto &in_exch_K = values[23];
	auto &par_max_diff = values[24];
	auto &par_is_soil = values[25];
	auto &par_depth = values[26];
	auto &par_temperature = values[27];
	auto &par_pco2 = values[28];
	auto &par_conc_doc = values[29];
	auto &par_log10_aloh3_equilibrium_const = values[30];
	auto &par_h_aloh3_exponent = values[31];
	auto &par_pk1doc = values[32];
	auto &par_pk2doc = values[33];
	auto &par_pk3doc = values[34];
	auto &par_pk1aldoc = values[35];
	auto &par_pk2aldoc = values[36];
	auto &par_porosity = values[37];
	auto &par_bulk_density = values[38];
	auto &par_cec = values[39];
	auto &par_so4_half_sat = values[40];
	auto &par_so4_max_cap = values[41];
	
	magic_init_input  input = {};
	magic_param       param = {};
	magic_init_result result = {};
	
	input.conc_Ca = in_conc_Ca.at(0) / 2.0;
	input.conc_Mg = in_conc_Mg.at(0) / 2.0;
	input.conc_Na = in_conc_Na.at(0);
	input.conc_K  = in_conc_K.at(0);
	input.conc_NH4 = in_conc_NH4.at(0);
	input.all_SO4 = in_all_SO4.at(0) / 2.0;
	input.conc_Cl = in_conc_Cl.at(0);
	input.conc_NO3 = in_conc_NO3.at(0);
	input.all_F = in_all_F.at(0);
	input.conc_PO4 = in_conc_PO4.at(0) / 3.0;
	input.exchangeable_Ca = 0.01*in_exch_Ca.at(0);
	input.exchangeable_Mg = 0.01*in_exch_Mg.at(0);
	input.exchangeable_Na = 0.01*in_exch_Na.at(0);
	input.exchangeable_K  = 0.01*in_exch_K.at(0);
	
	param.Depth              = par_depth.at(0);
	param.Temperature        = par_temperature.at(0);
	param.PartialPressureCO2 = par_pco2.at(0);
	param.conc_DOC           = par_conc_doc.at(0);
	param.Log10AlOH3EquilibriumConst = par_log10_aloh3_equilibrium_const.at(0);
	param.HAlOH3Exponent     = par_h_aloh3_exponent.at(0);
	param.pK1DOC             = par_pk1doc.at(0);
	param.pK2DOC             = par_pk2doc.at(0);
	param.pK3DOC             = par_pk3doc.at(0);
	param.pK1AlDOC           = par_pk1aldoc.at(0);
	param.pK2AlDOC           = par_pk2aldoc.at(0);
	param.Porosity           = par_porosity.at(0);
	param.BulkDensity        = par_bulk_density.at(0);
	param.CationExchangeCapacity = par_cec.at(0);
	param.SO4HalfSat         = par_so4_half_sat.at(0);
	param.SO4MaxCap          = par_so4_max_cap.at(0);
	
	bool is_soil = par_is_soil.bool_at(0);
	double conv = par_max_diff.at(0);
	
	MagicCoreInitial(input, param, result, is_soil, conv);
	
	out_conc_SO4.at(0) = result.conc_SO4 * 2.0;
	out_conc_F.at(0) = result.conc_F;
	out_conc_H.at(0) = result.conc_H;
	out_log10_ca_al.at(0) = result.Log10CaAlSelectCoeff;
	out_log10_mg_al.at(0) = result.Log10MgAlSelectCoeff;
	out_log10_na_al.at(0) = result.Log10NaAlSelectCoeff;
	out_log10_k_al.at(0) = result.Log10KAlSelectCoeff;
	out_ph.at(0) = result.pH;
	out_ionic_str.at(0) = result.IonicStrength;
	out_exch_so4.at(0) = 100.0*result.exchangeable_SO4;
}