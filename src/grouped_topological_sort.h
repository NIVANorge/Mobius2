
#ifndef MOBIUS_GROUPED_TOPOLOGICAL_SORT_H
#define MOBIUS_GROUPED_TOPOLOGICAL_SORT_H

#include <stdint.h>

struct Node_Group {
	u64 label;
	std::vector<int> nodes;
};

struct Sort_Predicate {
	virtual bool is_valid(int) = 0;
	virtual bool depends(int, int) = 0;
	virtual bool blocks(int, int) = 0;
	virtual uint64_t label(int) = 0;
};











#endif // MOBIUS_GROUPED_TOPOLOGICAL_SORT_H