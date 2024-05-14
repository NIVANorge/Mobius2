
#include "../external_computations.h"
#include "MAGIC_Core.h"

extern "C" DLLEXPORT void
magic_core(
	
	Value_Access *out_conc_ca,
	Value_Access *out_conc_mg,
	Value_Access *out_conc_na,
	Value_Access *out_conc_k,
	Value_Access *out_conc_nh4,
	Value_Access *out_conc_so4,
	Value_Access *out_conc_cl,
	Value_Access *out_conc_no3,
	Value_Access *out_conc_f,
	Value_Access *out_conc_po4,
	Value_Access *out_conc_all_so4,
	Value_Access *out_conc_all_f,
	Value_Access *out_ph,
	Value_Access *out_exch_ca,
	Value_Access *out_exch_mg,
	Value_Access *out_exch_na,
	Value_Access *out_exch_k,
	Value_Access *out_conc_hco3,
	Value_Access *out_conc_co3,
	Value_Access *out_all_doc,
	Value_Access *out_all_dic,
	
	Value_Access *io_conc_h,
	Value_Access *io_ionic_strength,

	Value_Access *in_total_ca,
	Value_Access *in_total_mg,
	Value_Access *in_total_na,
	Value_Access *in_total_k,
	Value_Access *in_total_nh4,
	Value_Access *in_total_so4,
	Value_Access *in_total_cl,
	Value_Access *in_total_no3,
	Value_Access *in_total_f,
	Value_Access *in_total_po4,
	
	Value_Access *par_depth,
	Value_Access *par_temperature,
	Value_Access *par_pco2,
	Value_Access *par_conc_doc,
	Value_Access *par_log10_aloh3_equilibrium_const,
	Value_Access *par_h_aloh3_exponent,
	Value_Access *par_pk1doc,
	Value_Access *par_pk2doc,
	Value_Access *par_pk3doc,
	Value_Access *par_pk1aldoc,
	Value_Access *par_pk2aldoc,
	Value_Access *par_porosity,
	Value_Access *par_bulk_density,
	Value_Access *par_cec,
	Value_Access *par_so4_half_sat,
	Value_Access *par_so4_max_cap,
	Value_Access *par_log10_ca_al,
	Value_Access *par_log10_mg_al,
	Value_Access *par_log10_na_al,
	Value_Access *par_log10_k_al
) {

	magic_input input = {};
	magic_param param = {};
	magic_output result = {};
	
	input.total_Ca = in_total_ca->at(0);
	input.total_Mg = in_total_mg->at(0);
	input.total_Na = in_total_na->at(0);
	input.total_K  = in_total_k->at(0);
	input.total_NH4 = in_total_nh4->at(0);
	input.total_SO4 = in_total_so4->at(0);
	input.total_Cl = in_total_cl->at(0);
	input.total_NO3 = in_total_no3->at(0);
	input.total_F = in_total_f->at(0);
	input.total_PO4 = in_total_po4->at(0);
	
	param.Depth              = par_depth->at(0);
	param.Temperature        = par_temperature->at(0);
	param.PartialPressureCO2 = par_pco2->at(0);
	param.conc_DOC           = par_conc_doc->at(0);
	param.Log10AlOH3EquilibriumConst = par_log10_aloh3_equilibrium_const->at(0);
	param.HAlOH3Exponent     = par_h_aloh3_exponent->at(0);
	param.pK1DOC             = par_pk1doc->at(0);
	param.pK2DOC             = par_pk2doc->at(0);
	param.pK3DOC             = par_pk3doc->at(0);
	param.pK1AlDOC           = par_pk1aldoc->at(0);
	param.pK2AlDOC           = par_pk2aldoc->at(0);
	param.Porosity           = par_porosity->at(0);
	param.BulkDensity        = par_bulk_density->at(0);
	param.CationExchangeCapacity = par_cec->at(0);
	param.SO4HalfSat         = par_so4_half_sat->at(0);
	param.SO4MaxCap          = par_so4_max_cap->at(0);
	param.Log10CaAlSelectCoeff = par_log10_ca_al->at(0);
	param.Log10MgAlSelectCoeff = par_log10_mg_al->at(0);
	param.Log10NaAlSelectCoeff = par_log10_na_al->at(0);
	param.Log10KAlSelectCoeff  = par_log10_k_al->at(0);
	
	s64 time_step = 0; //TODO!
	s64 comp_index = 0; //TODO!
	bool is_soil = true; //TODO!
	double conv = 1.0; //TODO!
	
	//double h_est = io_conc_h->at(0);
	
	double h_est = 1.0; 
	double ionic_strength = 0.0;
	
	//double ionic_strength = io_ionic_strength->at(0);
	
	MagicCore(input, param, result, is_soil, conv, h_est, ionic_strength, time_step, comp_index);
	
	// TODO: Maybe export more of the results (Al for instance)
	
	out_conc_ca->at(0) = result.conc_Ca;
	out_conc_mg->at(0) = result.conc_Mg;
	out_conc_na->at(0) = result.conc_Na;
	out_conc_k->at(0) = result.conc_K;
	out_conc_nh4->at(0) = result.conc_NH4;
	out_conc_so4->at(0) = result.conc_SO4;
	out_conc_cl->at(0) = result.conc_Cl;
	out_conc_no3->at(0) = result.conc_NO3;
	out_conc_f->at(0) = result.conc_F;
	out_conc_po4->at(0) = result.conc_PO4;
	
	out_conc_all_so4->at(0) = result.all_SO4;
	out_conc_all_f->at(0) = result.all_F;
	
	out_ph->at(0) = result.pH;
	out_exch_ca->at(0) = result.exchangeable_Ca;
	out_exch_mg->at(0) = result.exchangeable_Ca;
	out_exch_na->at(0) = result.exchangeable_Ca;
	out_exch_k->at(0) = result.exchangeable_Ca;
	
	out_conc_hco3->at(0) = result.conc_HCO3,
	out_conc_co3->at(0) = result.conc_CO3,
	
	out_all_doc->at(0) = result.all_DOC,
	out_all_dic->at(0) = result.all_DIC,
	
	io_conc_h->at(0) = result.conc_H;
	io_ionic_strength->at(0) = result.IonicStrength;
}