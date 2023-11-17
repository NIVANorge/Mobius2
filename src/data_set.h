
#ifndef MOBIUS_DATA_SET_H
#define MOBIUS_DATA_SET_H

#include <unordered_map>
#include <vector>

#include "linear_memory.h"
#include "ast.h"
#include "units.h"
#include "index_data.h"

// NOTE: the idea is that the Data_Set class should not have to know about the rest of the framework except for what is needed for the ast parser and lexer. This is just a self-contained parsed version of the data in the data files, that can then later be combined with a Mobius_Application to form a runnable model.

struct Data_Set;
typedef Index_Tuple<Data_Set> Indexes_D;


// Hmm, this is the n'th time we make something like this.
// But it is not that trivial to just merge them since there are slight differences in functionality needed.
template<Reg_Type reg_type, typename Info_Type> struct
Info_Registry {
	
	std::unordered_map<std::string, Entity_Id>        name_to_id;
	std::vector<Info_Type> data;
	
	//bool has(String_View name) { return name_to_id.find(name) != name_to_id.end(); }
	
	Info_Type *operator[](Entity_Id id) {
		if(id.id < 0 || id.id >= data.size())
			fatal_error(Mobius_Error::internal, "Tried to look up data set info using an invalid index.\n");
		return &data[id.id];
	}
	Entity_Id expect_exists_idx(Token *name, String_View info_type) {
		auto find = name_to_id.find(name->string_value);
		if(find == name_to_id.end()) {
			name->print_error_header();
			fatal_error("\"", name->string_value, "\" does not name an already declared ", info_type, ".");
		}
		return find->second;
	}
	Info_Type *expect_exists(Token *name, String_View info_type) {
		return &data[expect_exists_idx(name, info_type).id];
	}
	Entity_Id find_idx(String_View name) {
		auto find = name_to_id.find(name);
		if(find == name_to_id.end())
			return invalid_entity_id;
		return find->second;
	}
	Info_Type *find(String_View name) {
		auto id = find_idx(name);
		if(is_valid(id)) return &data[id.id];
		return nullptr;
	}
	Entity_Id create(const std::string &name, Source_Location loc) {
		check_allowed_serial_name(name, loc);
		auto find = name_to_id.find(name);
		if(find != name_to_id.end()) {
			loc.print_error_header();
			fatal_error("Re-declaration of \"", name, "\".");
		}
		Entity_Id id = Entity_Id { reg_type, (s16)data.size() };
		name_to_id[name] = id;
		data.push_back({});
		data.back().id = id;
		data.back().name = name;
		data.back().source_loc = loc;
		return id;
	}
	s64 count() { return data.size(); }
	void clear() {
		name_to_id.clear();
		data.clear();
	}
	
	Info_Type *begin() { return data.data(); }
	Info_Type *end()   { return data.data() + data.size(); }
};

struct
Info_Type_Base {
	Entity_Id id;
	std::string name;
	Source_Location source_loc;
};

struct
Index_Set_Info : Info_Type_Base {

	Entity_Id sub_indexed_to = invalid_entity_id;
	std::vector<Entity_Id> union_of;
	Entity_Id is_edge_of_connection = invalid_entity_id;
};

struct Component_Info : Info_Type_Base {
	Decl_Type decl_type;
	std::string handle;
	std::vector<Entity_Id> index_sets;
	bool can_have_edge_index = false;
};

struct Compartment_Ref {
	Entity_Id id = invalid_entity_id;   // This is the id of the Component_Info. invalid if it is an 'out'
	Indexes_D indexes;
};

inline bool operator==(const Compartment_Ref &a, const Compartment_Ref &b) {
	return a.id == b.id && a.indexes == b.indexes;
}

struct
Connection_Info : Info_Type_Base {    // This must either be subclased or have different data when we implement other connection structure types.

	enum class Type {
		none,
		directed_graph,
	} type;
	std::vector<std::pair<Compartment_Ref, Compartment_Ref>> arrows;
	
	Entity_Id edge_index_set = invalid_entity_id;
	
	Info_Registry<Reg_Type::component, Component_Info>        components;
	std::unordered_map<std::string, Entity_Id> component_handle_to_id; // Hmm, a bit annoying that we have to keep a separate one of these...
	
	Connection_Info() : type(Type::none) {}
};

struct
Par_Info : Info_Type_Base {
	Decl_Type type;
	std::vector<Parameter_Value> values;
	std::vector<std::string> values_enum; // Can't resolve them to int without knowledge of the model, which we on purpose don't have here.
	bool mark_for_deletion = false;
	int get_count() {
		if(type == Decl_Type::par_enum) return values_enum.size();
		else                            return values.size();
	}
};

struct
Par_Group_Info : Info_Type_Base {
	bool error = false;
	std::vector<Entity_Id> index_sets;
	Info_Registry<Reg_Type::parameter, Par_Info> pars;
};

struct
Module_Info : Info_Type_Base {
	Module_Version version;
	
	Info_Registry<Reg_Type::par_group, Par_Group_Info>   par_groups;
	Info_Registry<Reg_Type::connection, Connection_Info>  connections;
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
	std::vector<Indexes_D> indexes;
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
	
	Data_Set() : index_data(this) {
		// Default to one day.
		time_step_unit.declared_form.push_back({0, 1, Compound_Unit::day});
		time_step_unit.set_standard_form();
	}
	
	void read_from_file(String_View file_name);
	void write_to_file(String_View file_name);
	
	void generate_index_data(const std::string &name, const std::string &sub_indexed_to, const std::vector<std::string> &union_of);
	
	std::string main_file;
	std::string doc_string;
	
	// TODO: Just put the global_module into the modules Info_Registry ... Could simplify some code.
	Module_Info                     global_module;   // This is for par groups and connections that are not in a module but were declared in the model directly.
	Info_Registry<Reg_Type::index_set, Index_Set_Info>   index_sets;
	Info_Registry<Reg_Type::module, Module_Info>      modules;
	std::vector<Series_Set_Info>    series;
	
	Index_Data<Data_Set>            index_data;
	
	Source_Location                 unit_source_loc;
	Unit_Data                       time_step_unit;
	bool                            time_step_was_provided = false;
};


#endif // MOBIUS_DATA_SET_H