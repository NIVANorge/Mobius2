
#ifndef MOBIUS_PARAMETER_EDITING_H
#define MOBIUS_PARAMETER_EDITING_H

#include "common_types.h"

struct Model_Application;

struct Indexed_Parameter {
	bool valid = false;
	bool virt  = false;
	Entity_Id id;
	std::vector<Index_T> indexes;
	std::vector<u8> locks;     //NOTE: should be bool, but that has weird behavior.
	// std::string symbol;
	// std::string expr;
};

void
set_parameter_value(const Indexed_Parameter &par_data, Model_Application *app, Parameter_Value val);


#endif // MOBIUS_PARAMETER_EDITING_H