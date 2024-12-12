
#ifndef MOBIUS_SPECIAL_COMPUTATION_H
#define MOBIUS_SPECIAL_COMPUTATION_H

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

//#include "common_types.h"
#include "mobius_common.h"

struct
Value_Access {
	double *val;
	s64 stride;
	s64 count;
	
	inline double &at(s64 idx) { return val[idx*stride]; }
	inline u64    &bool_at(s64 idx) { return ((u64*)val)[idx*stride]; }
	inline s64    &int_at(s64 idx) { return ((s64*)val)[idx*stride]; }
};


// TODO: This should not be here..
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
	Value_Access *out_exch_so4, // new
	Value_Access *out_bss,     // new
	Value_Access *out_conc_hco3,
	Value_Access *out_conc_co3,
	Value_Access *out_all_doc,
	Value_Access *out_all_dic,
	Value_Access *out_conc_al, // new
	Value_Access *out_all_al, // new
	Value_Access *out_org_al, // new
	
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
	
	Value_Access *par_max_diff,
	Value_Access *par_is_soil,
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
);

extern "C" DLLEXPORT void
magic_core_initial(
	Value_Access *out_conc_SO4,
	Value_Access *out_conc_F,
	Value_Access *out_conc_H,
	Value_Access *out_log10_ca_al,
	Value_Access *out_log10_mg_al,
	Value_Access *out_log10_na_al,
	Value_Access *out_log10_k_al,
	Value_Access *out_ph,
	Value_Access *out_ionic_str,
	Value_Access *out_exch_so4,

	Value_Access *in_conc_Ca,
	Value_Access *in_conc_Mg,
	Value_Access *in_conc_Na,
	Value_Access *in_conc_K,
	Value_Access *in_conc_NH4,
	Value_Access *in_all_SO4,
	Value_Access *in_conc_Cl,
	Value_Access *in_conc_NO3,
	Value_Access *in_all_F,
	Value_Access *in_conc_PO4,
	Value_Access *in_exch_Ca,
	Value_Access *in_exch_Mg,
	Value_Access *in_exch_Na,
	Value_Access *in_exch_K,
	
	Value_Access *par_max_diff,
	Value_Access *par_is_soil,
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
	Value_Access *par_so4_max_cap
);



#endif // MOBIUS_SPECIAL_COMPUTATION_H