
#ifndef MOBIUS_RESIZE_DATA_SET_H
#define MOBIUS_RESIZE_DATA_SET_H

#include "../data_set.h"

struct
New_Indexes {
	char *index_set;
	std::vector<std::pair<Token, std::vector<Token>>> data;
};

void
resize_data_set(
	Data_Set *data_set,
	std::vector<New_Indexes> &new_indexes
	// TODO: Connection data
);


#endif // MOBIUS_RESIZE_DATA_SET_H