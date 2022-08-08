

#ifndef MOBIUS_FILE_UTILS_H
#define MOBIUS_FILE_UTILS_H

#include <stdio.h>
#include <locale>
#include <codecvt>

#include "mobius_common.h"
#include "linear_memory.h"

FILE *
open_file(String_View file_name, String_View mode);

String_View
read_entire_file(String_View file_name);

String_View
make_path_relative_to(String_View file_name, String_View relative_to);

String_View
get_extension(String_View file_name, bool *success);


struct
File_Data_Handler {
	Linear_Allocator allocator; // this is just for storing file names.. TODO: could just use std::string for the key here though (but not the file data)
	string_map<String_View> loaded_files;
	
	File_Data_Handler() : allocator(1024*1024) {};
	
	String_View
	load_file(String_View file_name, String_View relative = {}, String_View *normalized_path_out = nullptr) {
		String_View load_name = file_name;
		if(relative)
			load_name = make_path_relative_to(file_name, relative);
		auto find = loaded_files.find(load_name);
		if(find != loaded_files.end()) {
			if(normalized_path_out) *normalized_path_out = find->first;
			return find->second;
		}
		load_name = allocator.copy_string_view(load_name);
		String_View data = read_entire_file(load_name);
		loaded_files[load_name] = data;
		if(normalized_path_out) *normalized_path_out = load_name;
		
		return data;
	}
	
	bool
	is_loaded(String_View file_name, String_View relative) {
		String_View load_name = file_name;
		if(relative)
			load_name = make_path_relative_to(file_name, relative);
		auto find = loaded_files.find(load_name);
		return find != loaded_files.end();
	}
	
	void
	unload(String_View file_name, String_View relative) {
		String_View load_name = file_name;
		if(relative)
			load_name = make_path_relative_to(file_name, relative);
		auto find = loaded_files.find(load_name);
		if(find != loaded_files.end()) {
			delete find->second.data;
			loaded_files.erase(find);
		}
	}
	
	~File_Data_Handler() {
		for(auto find : loaded_files) free(find.second.data);
	}
};


#endif