

#ifndef MOBIUS_FILE_UTILS_H
#define MOBIUS_FILE_UTILS_H

#include <string>
#include <unordered_map>
#include <stdio.h>

#include "common_types.h"

FILE *
open_file(String_View file_name, String_View mode);

String_View
read_entire_file(String_View file_name, Source_Location from = {});

String_View
make_path_relative_to(String_View file_name, String_View relative_to);

String_View
get_extension(String_View file_name, bool *success);

std::string
standardize_base_path(String_View path);

bool
bottom_directory_is(String_View path, String_View directory);

struct
File_Data_Handler {

	std::unordered_map<std::string, String_View> loaded_files;
	
	String_View
	load_file(String_View file_name, Source_Location from = {}, String_View relative_to = {}, std::string *normalized_path_out = nullptr);
	
	inline bool
	is_loaded(String_View file_name, String_View relative_to) {
		String_View load_name = file_name;
		if(relative_to.count)
			load_name = make_path_relative_to(file_name, relative_to);
		auto find = loaded_files.find(load_name);
		return find != loaded_files.end();
	}
	
	inline void
	unload(String_View file_name, String_View relative_to = {}) {
		String_View load_name = file_name;
		if(relative_to.count)
			load_name = make_path_relative_to(file_name, relative_to);
		auto find = loaded_files.find(load_name);
		if(find != loaded_files.end()) {
			free(find->second.data);
			loaded_files.erase(find);
		}
	}
	
	inline void
	unload_all() {
		for(auto find : loaded_files) free(find.second.data);
		loaded_files.clear();
	}
	
	~File_Data_Handler() {  unload_all();  }
};


#endif // MOBIUS_FILE_UTILS_H