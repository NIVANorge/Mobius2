
#ifndef MOBIUS_DATA_SET_H
#define MOBIUS_DATA_SET_H

#include <unordered_map>
#include <vector>

#include "linear_memory.h"
#include "ast.h"

// NOTE: the idea is that this class should not have to know about the rest of the framework except for what is needed for the ast parser and lexer.


// Hmm, this is the n'th time we make something like this.
// But it is not that trivial to just merge them since there are slight differences in functionality needed.
template<typename Info_Type> struct
Info_Registry {
	
	string_map<int>        name_to_id;
	std::vector<Info_Type> data;
	
	bool has(String_View name) { return name_to_id.find(name) != name_to_id.end(); }
	/*
	int operator[](String_View name) {
		auto find = name_to_id.find(name);
		if(find != name_to_id.end())
			return find->second;
		return -1;
	}
	*/
	Info_Type *operator[](int idx) {
		if(idx < 0 || idx >= data.size())
			fatal_error(Mobius_Error::internal, "Tried to look up data set info using an invalid index.\n");
		return &data[idx];
	}
	int expect_exists_idx(Token *name, String_View info_type) {
		auto find = name_to_id.find(name->string_value);
		if(find == name_to_id.end()) {
			name->print_error_header();
			fatal_error("\"", name->string_value, "\" does not name an already declared ", info_type, ".");
		}
		return find->second;
	}
	Info_Type *expect_exists(Token *name, String_View info_type) {
		return &data[expect_exists_idx(name, info_type)];
	}
	Info_Type *find_or_create(Token *name, bool allow_duplicate=false) {
		auto find = name_to_id.find(name->string_value);
		if(find != name_to_id.end()) {
			if(!allow_duplicate) {
				name->print_error_header();
				fatal_error("Re-declaration of \"", name, "\".");
			}
			return &data[find->second];
		}
		name_to_id[name->string_value] = (int)data.size();
		data.push_back({});
		data.back().name = *name;
		return &data.back();
	}
	int count() { return data.size(); }
	
	Info_Type *begin() { return data.data(); }
	Info_Type *end()   { return data.data() + data.size(); }
};

struct
Index_Info {
	Token name;
};

struct
Index_Set_Info {
	Token name;
	Info_Registry<Index_Info> indexes;
};

struct
Neighbor_Info {    // This obviously has to be either subclased or have different data when having other neighbor structure types.
	Token name;
	String_View index_set;
	
	enum class Type {
		none,
		graph,
		} type;
	std::vector<int> points_at;
	
	Neighbor_Info() : type(Type::none) {}
};

struct
Par_Info {
	Token name;
	Decl_Type type;
	std::vector<Parameter_Value> values;
	std::vector<String_View> values_enum; // Can't resolve them to int yet.
};

struct
Par_Group_Info {
	Token name;
	
	std::vector<String_View> index_sets;
	Info_Registry<Par_Info> pars;
};

struct
Module_Info {
	Token name;
	int major, minor, revision;
	
	Info_Registry<Par_Group_Info> par_groups;
};

enum
Series_Data_Flags {
	series_data_none              = 0x00,
	series_data_interp_step       = 0x01,
	series_data_interp_linear     = 0x02,
	series_data_interp_spline     = 0x04,
	series_data_repeat_yearly     = 0x08,
	// TODO: could allow specifying an "series_data_override" to let this series override a state variable.
};

inline void
set_flag(Series_Data_Flags *flags, Token *token) {
	String_View name = token->string_value;
	if     (name == "interp_step")   *flags = (Series_Data_Flags)(*flags & series_data_interp_step);
	else if(name == "interp_linear") *flags = (Series_Data_Flags)(*flags & series_data_interp_linear);
	else if(name == "interp_spline") *flags = (Series_Data_Flags)(*flags & series_data_interp_spline);
	else if(name == "repeat_yearly") *flags = (Series_Data_Flags)(*flags & series_data_repeat_yearly);
	else {
		token->print_error_header();
		fatal_error("Unrecognized series data flag \"", name, "\".");
	}
}

struct
Series_Header_Info {
	Source_Location location;
	String_View name;
	std::vector<std::vector<std::pair<String_View, int>>> indexes;
	Series_Data_Flags flags;
	
	Series_Header_Info() : flags(series_data_none) {}
};

struct
Series_Set_Info {
	String_View file_name;
	Date_Time start_date;
	Date_Time end_date;
	s64       time_steps;
	bool has_date_vector;
	std::vector<Series_Header_Info>  header_data;
	std::vector<Date_Time>           dates;
	std::vector<double>              raw_values;
	
	Series_Set_Info() : has_date_vector(true) {};
};

struct
Data_Set {
	
	File_Data_Handler file_handler;
	
	Data_Set() {
		global_pars.name = {};
		global_pars.name.string_value = "System";
		global_pars.name.type = Token_Type::quoted_string;
	}
	//Data_Set(String_View file_name);
	
	void read_from_file(String_View file_name);
	void write_to_file(String_View file_name);
	
	String_View main_file;
	
	String_View doc_string;
	
	Par_Group_Info global_pars;     // This is typically just for "Start date" and "End date"
	Module_Info    global_module;   // This is for par groups that are not in a module but were declared in the model directly.
	
	Info_Registry<Index_Set_Info>  index_sets;
	Info_Registry<Neighbor_Info>   neighbors;
	Info_Registry<Module_Info>     modules;
	std::vector<Series_Set_Info>   series;
};


#endif // MOBIUS_DATA_SET_H