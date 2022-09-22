
#ifndef MOBIUS_PARAMETER_EDITING_H
#define MOBIUS_PARAMETER_EDITING_H

#include "common_types.h"

struct Model_Data;

struct Indexed_Parameter {
	bool virt  = false;
	Entity_Id id = invalid_entity_id;
	std::vector<Index_T> indexes;
	std::vector<u8> locks;     //NOTE: should be std::vector<bool>, but that has weird behavior.
	std::string symbol;
	std::string expr;
};

void
set_parameter_value(const Indexed_Parameter &par_data, Model_Data *data, Parameter_Value val);

bool
parameter_is_subset_of(const Indexed_Parameter &par, const Indexed_Parameter &compare_to);

#endif // MOBIUS_PARAMETER_EDITING_H