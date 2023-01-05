
#ifndef MOBIUS_DATA_SET_H
#define MOBIUS_DATA_SET_H

#include <unordered_map>
#include <vector>

#include "linear_memory.h"
#include "ast.h"
#include "units.h"

// NOTE: the idea is that this class should not have to know about the rest of the framework except for what is needed for the ast parser and lexer.


// Hmm, this is the n'th time we make something like this.
// But it is not that trivial to just merge them since there are slight differences in functionality needed.
template<typename Info_Type> struct
Info_Registry {
	
	std::unordered_map<std::string, int>        name_to_id;
	std::vector<Info_Type> data;
	
	//bool has(String_View name) { return name_to_id.find(name) != name_to_id.end(); }
	
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
	int find_idx(String_View name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			return -1;
		return find->second;
	}
	Info_Type *find(String_View name) {
		int idx = find_idx(name);
		if(idx >= 0) return &data[idx];
		return nullptr;
	}
	Info_Type *create(const std::string &name, Source_Location loc) {
		auto find = name_to_id.find(name);
		if(find != name_to_id.end()) {
			loc.print_error_header();
			fatal_error("Re-declaration of \"", name, "\".");
		}
		name_to_id[name] = (int)data.size();
		data.push_back({});
		data.back().name = name;
		data.back().loc = loc;
		return &data.back();
	}
	int count() { return data.size(); }
	void clear() {
		name_to_id.clear();
		data.clear();
	}
	
	Info_Type *begin() { return data.data(); }
	Info_Type *end()   { return data.data() + data.size(); }
};

struct
Info_Type_Base {
	std::string name;
	Source_Location loc;
};

struct
Index_Info : Info_Type_Base {
};

struct
Index_Set_Info : Info_Type_Base {
	enum class Type {
		none,
		named,
		numeric1,
	} type;
	Info_Registry<Index_Info> indexes;
	int                       n_dim1;
	int get_count() {
		if(type == Type::named) return indexes.count();
		return n_dim1;
	}
	
	Index_Set_Info() : n_dim1(0), type(Type::none) {}
};

struct Component_Info : Info_Type_Base {
	Decl_Type decl_type;
	std::string handle;
	std::vector<int> index_sets;
};

struct Compartment_Ref {
	int id;
	std::vector<int> indexes;
};

inline bool operator==(const Compartment_Ref &a, const Compartment_Ref &b) {
	return a.id == b.id && a.indexes == b.indexes;
}

struct
Connection_Info : Info_Type_Base {    // This must either be subclased or have different data when we implement other connection structure types.

	enum class Type {
		none,
		graph,
	} type;
	std::vector<std::pair<Compartment_Ref, Compartment_Ref>> arrows;
	
	Info_Registry<Component_Info>        components;
	std::unordered_map<std::string, int> component_handle_to_id; // Hmm, a bit annoying that we have to keep a separate one of these...
	
	Connection_Info() : type(Type::none) {}
};

struct
Par_Info : Info_Type_Base {
	Decl_Type type;
	std::vector<Parameter_Value> values;
	std::vector<std::string> values_enum; // Can't resolve them to int without knowledge of the model, which we on purpose don't have here.
};

struct
Par_Group_Info : Info_Type_Base {
	std::vector<int> index_sets;
	Info_Registry<Par_Info> pars;
};

struct
Module_Info : Info_Type_Base {
	Module_Version version;
	
	Info_Registry<Par_Group_Info> par_groups;
};

enum
Series_Data_Flags {
	series_data_none              = 0x00,
	series_data_interp_step       = 0x01,
	series_data_interp_linear     = 0x02,
	series_data_interp_spline     = 0x04,
	series_data_interp_inside     = 0x08,
	series_data_repeat_yearly     = 0x10,
	// TODO: could allow specifying an "series_data_override" to let this series override a state variable.
};

inline bool
set_flag(Series_Data_Flags *flags, String_View name) {
	if     (name == "step_interpolate")   *flags = (Series_Data_Flags)(*flags | series_data_interp_step);
	else if(name == "linear_interpolate") *flags = (Series_Data_Flags)(*flags | series_data_interp_linear);
	else if(name == "spline_interpolate") *flags = (Series_Data_Flags)(*flags | series_data_interp_spline);
	else if(name == "inside")        *flags = (Series_Data_Flags)(*flags | series_data_interp_inside);
	else if(name == "repeat_yearly") *flags = (Series_Data_Flags)(*flags | series_data_repeat_yearly);
	else
		return false;
	return true;
}

struct
Series_Header_Info : Info_Type_Base {
	std::vector<std::vector<std::pair<std::string, int>>> indexes;
	Series_Data_Flags flags;
	Unit_Data         unit;
	
	Series_Header_Info() : flags(series_data_none) {}
};

struct
Series_Set_Info {
	std::string file_name;
	Date_Time start_date;
	Date_Time end_date;    // Invalid if the series doesn't have a date vector.
	s64       time_steps;  // For series that don't have a date vector.
	bool has_date_vector;
	std::vector<Series_Header_Info>  header_data;
	std::vector<Date_Time>           dates;
	std::vector<std::vector<double>> raw_values;
	
	Series_Set_Info() : has_date_vector(true) {};
};

struct
Data_Set {
	
	File_Data_Handler file_handler;
	
	Data_Set() {
		// Default to one day.
		time_step_size.unit      = Time_Step_Size::second;
		time_step_size.magnitude = 86400;
	}
	
	void read_from_file(String_View file_name);
	void write_to_file(String_View file_name);
	
	std::string main_file;
	std::string doc_string;
	
	Module_Info                     global_module;   // This is for par groups that are not in a module but were declared in the model directly.
	Info_Registry<Index_Set_Info>   index_sets;
	Info_Registry<Connection_Info>  connections;
	Info_Registry<Module_Info>      modules;
	std::vector<Series_Set_Info>    series;
	
	Time_Step_Size                  time_step_size;
	bool                            time_step_was_provided = false;
};


#endif // MOBIUS_DATA_SET_H