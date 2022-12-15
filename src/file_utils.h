

#ifndef MOBIUS_FILE_UTILS_H
#define MOBIUS_FILE_UTILS_H

#include <stdio.h>
#include <locale>
#include <codecvt>

#include "mobius_common.h"
#include "linear_memory.h"
#include "common_types.h"

FILE *
open_file(String_View file_name, String_View mode);

String_View
read_entire_file(String_View file_name, Source_Location from);

String_View
make_path_relative_to(String_View file_name, String_View relative_to);

String_View
get_extension(String_View file_name, bool *success);


struct
File_Data_Handler {
	Linear_Allocator allocator; // this is just for storing file names. But for now this is needed, as it would be tricky wrt other parts of the implementation to switch file names to std::string.
	string_map<String_View> loaded_files;
	
	File_Data_Handler() : allocator(1024*1024) {};
	
	String_View
	load_file(String_View file_name, Source_Location from = {}, String_View relative_to = {}, String_View *normalized_path_out = nullptr);
	
	bool
	is_loaded(String_View file_name, String_View relative_to) {
		String_View load_name = file_name;
		if(relative_to)
			load_name = make_path_relative_to(file_name, relative_to);
		auto find = loaded_files.find(load_name);
		return find != loaded_files.end();
	}
	
	void
	unload(String_View file_name, String_View relative_to) {
		String_View load_name = file_name;
		if(relative_to)
			load_name = make_path_relative_to(file_name, relative_to);
		auto find = loaded_files.find(load_name);
		if(find != loaded_files.end()) {
			free(find->second.data);
			loaded_files.erase(find);
		}
	}
	
	void
	unload_all() {
		for(auto find : loaded_files) free(find.second.data);
		loaded_files.clear();
	}
	
	~File_Data_Handler() {  unload_all();  }
};


#endif