
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
	
	input.total_Ca  = *in_total_ca;
	input.total_Mg  = *in_total_mg;
	input.total_Na  = *in_total_na;
	input.total_K   = *in_total_k;
	input.total_NH4 = *in_total_nh4;
	input.total_SO4 = *in_total_so4;
	input.total_Cl  = *in_total_cl;
	input.total_NO3 = *in_total_no3;
	input.total_F   = *in_total_f;
	input.total_PO4 = *in_total_po4;
	
	param.Depth              = *par_depth;
	param.Temperature        = *par_temperature;
	param.PartialPressureCO2 = *par_pco2;
	param.conc_DOC           = *par_conc_doc;
	param.Log10AlOH3EquilibriumConst = *par_log10_aloh3_equilibrium_const;
	param.HAlOH3Exponent     = *par_h_aloh3_exponent;
	param.pK1DOC             = *par_pk1doc;
	param.pK2DOC             = *par_pk2doc;
	param.pK3DOC             = *par_pk3doc;
	param.pK1AlDOC           = *par_pk1aldoc;
	param.pK2AlDOC           = *par_pk2aldoc;
	param.Porosity           = *par_porosity;
	param.BulkDensity        = *par_bulk_density;
	param.CationExchangeCapacity = *par_cec;
	param.SO4HalfSat         = *par_so4_half_sat;
	param.SO4MaxCap          = *par_so4_max_cap;
	param.Log10CaAlSelectCoeff = *par_log10_ca_al;
	param.Log10MgAlSelectCoeff = *par_log10_mg_al;
	param.Log10NaAlSelectCoeff = *par_log10_na_al;
	param.Log10KAlSelectCoeff  = *par_log10_k_al;
	
	// NOTE: time_step and comp_index were only used in an error message, but we had to disable that for now since we can't throw errors from model code anymore.
	s64 time_step = 0;
	s64 comp_index = 0;
	
	bool is_soil = par_is_soil.bool_at(0);
	double conv = *par_max_diff;
	
	double h_est = *io_conc_h;
	double ionic_strength = *io_ionic_strength;
	
	MagicCore(input, param, result, is_soil, conv, h_est, ionic_strength, time_step, comp_index);
	
	// NOTE: Converting mmol to meq
	*out_conc_ca = 2.0*result.conc_Ca;
	*out_conc_mg = 2.0*result.conc_Mg;
	*out_conc_na = result.conc_Na;
	*out_conc_k = result.conc_K;
	*out_conc_nh4 = result.conc_NH4;
	*out_conc_so4 = 2.0*result.conc_SO4;
	*out_conc_cl = result.conc_Cl;
	*out_conc_no3 = result.conc_NO3;
	*out_conc_f = result.conc_F;
	*out_conc_po4 = 3.0*result.conc_PO4;
	
	*out_conc_all_so4 = result.all_SO4 * 2.0;
	*out_conc_all_f = result.all_F;
	
	*out_ph = result.pH;
	
	// NOTE: Converting fraction to percent
	*out_exch_ca = 100.0*result.exchangeable_Ca;
	*out_exch_mg  = 100.0*result.exchangeable_Mg;
	*out_exch_na  = 100.0*result.exchangeable_Na;
	*out_exch_k   = 100.0*result.exchangeable_K;
	*out_exch_so4 = 100.0*result.exchangeable_SO4;
	*out_bss      = 100.0*result.BaseSaturationSoil;
	
	*out_conc_hco3 = result.conc_HCO3,
	*out_conc_co3  = result.conc_CO3,
	
	*out_all_doc = result.all_DOC,
	*out_all_dic = result.all_DIC,
	
	*out_conc_al = result.conc_Al;
	*out_all_al = result.all_Al;
	*out_org_al = result.org_Al;
	
	*io_conc_h = result.conc_H;
	*io_ionic_strength = result.IonicStrength;
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
	
	input.conc_Ca = *in_conc_Ca / 2.0;
	input.conc_Mg = *in_conc_Mg / 2.0;
	input.conc_Na = *in_conc_Na;
	input.conc_K  = *in_conc_K;
	input.conc_NH4 = *in_conc_NH4;
	input.all_SO4 = *in_all_SO4 / 2.0;
	input.conc_Cl = *in_conc_Cl;
	input.conc_NO3 = *in_conc_NO3;
	input.all_F = *in_all_F;
	input.conc_PO4 = *in_conc_PO4 / 3.0;
	input.exchangeable_Ca = *in_exch_Ca * 0.01;
	input.exchangeable_Mg = *in_exch_Mg * 0.01;
	input.exchangeable_Na = *in_exch_Na * 0.01;
	input.exchangeable_K  = *in_exch_K * 0.01;
	
	param.Depth              = *par_depth;
	param.Temperature        = *par_temperature;
	param.PartialPressureCO2 = *par_pco2;
	param.conc_DOC           = *par_conc_doc;
	param.Log10AlOH3EquilibriumConst = *par_log10_aloh3_equilibrium_const;
	param.HAlOH3Exponent     = *par_h_aloh3_exponent;
	param.pK1DOC             = *par_pk1doc;
	param.pK2DOC             = *par_pk2doc;
	param.pK3DOC             = *par_pk3doc;
	param.pK1AlDOC           = *par_pk1aldoc;
	param.pK2AlDOC           = *par_pk2aldoc;
	param.Porosity           = *par_porosity;
	param.BulkDensity        = *par_bulk_density;
	param.CationExchangeCapacity = *par_cec;
	param.SO4HalfSat         = *par_so4_half_sat;
	param.SO4MaxCap          = *par_so4_max_cap;
	
	bool is_soil = par_is_soil.bool_at(0);
	double conv = *par_max_diff;
	
	MagicCoreInitial(input, param, result, is_soil, conv);
	
	*out_conc_SO4 = result.conc_SO4 * 2.0;
	*out_conc_F = result.conc_F;
	*out_conc_H = result.conc_H;
	*out_log10_ca_al = result.Log10CaAlSelectCoeff;
	*out_log10_mg_al = result.Log10MgAlSelectCoeff;
	*out_log10_na_al = result.Log10NaAlSelectCoeff;
	*out_log10_k_al = result.Log10KAlSelectCoeff;
	*out_ph = result.pH;
	*out_ionic_str = result.IonicStrength;
	*out_exch_so4 = 100.0*result.exchangeable_SO4;
}