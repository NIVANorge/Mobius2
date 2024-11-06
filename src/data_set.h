
#ifndef MOBIUS_DATASET_H
#define MOBIUS_DATASET_H

#include "catalog.h"
#include "index_data.h"

struct
Module_Data : Registration_Base {
	Module_Version version;
	Decl_Scope scope;
	
	void process_declaration(Catalog *catalog);
};

struct
Par_Group_Data : Registration_Base {
	std::vector<Entity_Id> index_sets;
	Decl_Scope scope;
	bool error = false;
	bool mark_for_deletion = false;
	
	void process_declaration(Catalog *catalog);
};

enum
Series_Data_Flags {
	series_data_none              = 0x00,
	series_data_interp_step       = 0x01,
	series_data_interp_linear     = 0x02,
	series_data_interp_spline     = 0x04,
	series_data_interp_inside     = 0x08,
	series_data_repeat_yearly     = 0x10,
};

inline bool
set_flag(Series_Data_Flags *flags, String_View name) {
	if     (name == "step_interpolate")   *flags = (Series_Data_Flags)(*flags | series_data_interp_step);
	else if(name == "linear_interpolate") *flags = (Series_Data_Flags)(*flags | series_data_interp_linear);
	else if(name == "spline_interpolate") *flags = (Series_Data_Flags)(*flags | series_data_interp_spline);
	else if(name == "inside")             *flags = (Series_Data_Flags)(*flags | series_data_interp_inside);
	else if(name == "repeat_yearly")      *flags = (Series_Data_Flags)(*flags | series_data_repeat_yearly);
	else
		return false;
	return true;
}

struct
Series_Header {
	Source_Location       source_loc;
	std::string           name;
	std::vector<Indexes>  indexes;
	Series_Data_Flags     flags;
	Unit_Data             unit;
	
	Series_Header() : flags(series_data_none) {}
};

struct
Series_Set {
	Date_Time                        start_date;
	Date_Time                        end_date;    // Invalid if the series doesn't have a date vector.
	s64                              time_steps;  // For series that don't have a date vector.
	bool has_date_vector;
	std::vector<Series_Header>       header_data;
	std::vector<Date_Time>           dates;
	std::vector<std::vector<double>> raw_values;
	
	Series_Set() : has_date_vector(true) {};
};

struct
Series_Data : Registration_Base {
	
	std::string file_name;
	std::vector<Series_Set> series; // Need multiple since there could be multiple tabs in an xlsx.
	
	void process_declaration(Catalog *catalog);
};


struct
Parmap_Entry {
	Indexes indexes;
	double pos;
	double value;
};

struct Data_Set;

struct
Parameter_Data : Registration_Base {
	std::vector<Parameter_Value> values;
	std::vector<std::string> values_enum; // Can't resolve them to int without knowledge of the model, which we on purpose don't have here.
	
	Entity_Id from_pos = invalid_entity_id;
	
	bool is_on_map_form = false;
	std::vector<Parmap_Entry> parmap_data;
	
	bool mark_for_deletion = false;
	int get_count();
	
	void process_declaration(Catalog *catalog);
	
	void unpack_parameter_map(Data_Set *data_set);
};

struct
Component_Data : Registration_Base {
	std::vector<Entity_Id> index_sets;
	bool can_have_edge_index_set = false;
	
	void process_declaration(Catalog *catalog);
};

struct Compartment_Ref {
	Entity_Id id = invalid_entity_id;   // This is the id of the Component_Info. invalid if it is an 'out'
	Indexes indexes;
};

inline bool operator==(const Compartment_Ref &a, const Compartment_Ref &b) {
	return a.id == b.id && a.indexes == b.indexes;
}

struct
Connection_Data : Registration_Base {
	
	Decl_Scope scope; // For components and things declared inside the connection declaration.
	
	enum class Type {
		none,
		directed_graph,
	} type;
	
	std::vector<std::pair<Compartment_Ref, Compartment_Ref>> arrows;
	Entity_Id edge_index_set = invalid_entity_id;
	
	void process_declaration(Catalog *catalog);
};

struct
Quick_Select {
	std::string name;
	std::vector<std::string> series_names;
};

struct
Quick_Select_Data : Registration_Base {
	std::vector<Quick_Select> selects;
	
	void process_declaration(Catalog *catalog);
};

struct
Position_Map_Data : Registration_Base {
	
	// NOTE: This is just in order to store sufficient info to save it out again.
	// The processed data that will be used is stored in the index_data.
	
	Entity_Id           index_set_id = invalid_entity_id;
	std::vector<double> pos_vals_raw;
	std::vector<double> width_vals_raw;
	//bool raw_is_widths = false;
	//bool linear_interp = false;
	
	void process_declaration(Catalog *catalog);
};

struct
Data_Set : Catalog {
	
	Data_Set() : index_data(this) {
		// Default to one day.
		time_step_unit.declared_form.push_back({0, 1, Compound_Unit::day});
		time_step_unit.set_standard_form();
	}
	
	Registry<Module_Data,       Reg_Type::module>     modules;
	Registry<Par_Group_Data,    Reg_Type::par_group>  par_groups;
	Registry<Parameter_Data,    Reg_Type::parameter>  parameters;
	Registry<Series_Data,       Reg_Type::series>     series;
	Registry<Component_Data,    Reg_Type::component>  components;
	Registry<Connection_Data,   Reg_Type::connection> connections;
	Registry<Quick_Select_Data, Reg_Type::quick_select> quick_selects;
	Registry<Position_Map_Data, Reg_Type::position_map> position_maps;
	
	Index_Data                      index_data;
	
	// NOTE: The step unit could be stored as a Registration, but we are ever only going to want to have one.
	Source_Location                 unit_source_loc;
	Unit_Data                       time_step_unit;
	bool                            time_step_was_provided = false;
	
	Date_Time                       series_begin;
	Date_Time                       series_end;
	bool                            series_interval_was_provided = false;
	
	
	void read_from_file(String_View file_name);
	void write_to_file(String_View file_name);
	void generate_index_data(const std::string &name, const std::string &identifier, const std::string &sub_indexed_to, const std::vector<std::string> &union_of);
	
	Registry_Base *registry(Reg_Type reg_type);
	Decl_Scope    *get_scope(Entity_Id scope_id);
	
	void get_model_options(Model_Options &options);
};


#endif // MOBIUS_DATASET_H