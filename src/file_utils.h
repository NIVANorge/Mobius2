

#ifndef MOBIUS_FILE_UTILS_H
#define MOBIUS_FILE_UTILS_H

#include <stdio.h>
#include <locale>
#include <codecvt>

#include "mobius_common.h"
#include "linear_memory.h"

inline FILE *
open_file(String_View file_name, String_View mode)
{
	// Wrapper to allow for non-ascii names on Windows. Assumes file_name is UTF8 formatted.
	FILE *file;
#ifdef _WIN32
	std::u16string filename16 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(file_name.data, file_name.data+file_name.count);
	std::u16string mode16     = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(mode.data, mode.data+mode.count);
	file = _wfopen((wchar_t *)filename16.data(), (wchar_t *)mode16.data());
#else
	std::string filename8(file_name.data, file_name.count);
	std::string mode8    (mode.data, mode.count);
	file = fopen(filename8.data(), mode8.data());
#endif

	if(!file)
		fatal_error(Mobius_Error::file, "Tried to open file \"", file_name, "\", but was not able to.");

	return file;
}

inline String_View
read_entire_file(String_View file_name)
{
	String_View file_data;
	
	FILE *file = open_file(file_name, "rb");

	fseek(file, 0, SEEK_END);
	file_data.count = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	if(file_data.count == 0) {
		fclose(file);
		fatal_error(Mobius_Error::file, "The file ", file_name, " is empty.");
	}
	
	char *data = (char *)malloc(file_data.count + 1);
	
	if(!data)
		fatal_error(Mobius_Error::file, "Unable to allocate enough memory to read in file ", file_name);

	size_t read_size = fread((void *)data, 1, file_data.count, file); // NOTE: Casting away constness, but it doesn't matter since we just allocated it ourselves.
	fclose(file);
	
	if(read_size != file_data.count) {
		free((void *)data);
		fatal_error(Mobius_Error::file, "Unable to read the entire file ", file_name);
	}
	
	data[file_data.count] = '\0';    // Zero-terminate it in case we want to interface with C libraries.
	file_data.data = data;
	
	return file_data;
}

inline String_View
make_path_relative_to(String_View file_name, String_View relative) {
	int last_slash;
	bool any_slash_at_all = false;
	for(last_slash = relative.count - 1; last_slash >= 0; --last_slash) {
		char c = file_name[last_slash];
		if(c == '\\' || c == '/') {
			any_slash_at_all = true;
			break;
		}
	}
	if(!any_slash_at_all) last_slash = -1;
	static char new_path[1024]; //TODO make a string builder instead?
	sprintf(new_path, "%.*s%.*s", last_slash+1, relative.data, (int)file_name.count, file_name.data);
	return new_path;
}

inline String_View
get_extension(String_View file_name, bool *success) {
	int len = file_name.count;
	String_View extension;
	extension.data = file_name.data+(len-1);
	extension.count = 0;
	while(*extension.data != '.' && extension.data != file_name.data) { --extension.data; ++extension.count; }
	*success = !(extension.data == file_name.data && *extension.data != '.');
	return extension;
}




struct
File_Data_Handler {
	Linear_Allocator allocator; // this is just for storing file names.. TODO: could just use std::string for the key here though (but not the file data)
	string_map<String_View> loaded_files;
	
	File_Data_Handler() : allocator(1024*1024) {};
	
	String_View
	load_file(String_View file_name, String_View relative = {}) {
		String_View load_name = file_name;
		if(relative)
			//TODO: This could possibly give us multiple paths pointing to the same file. We should standardize them more, but that would be system specific.
			// could maybe use std::filesystem::path here...
			make_path_relative_to(file_name, relative);
		auto find = loaded_files.find(load_name);
		if(find != loaded_files.end())
			return find->second;
		load_name = allocator.copy_string_view(load_name);
		String_View data = read_entire_file(load_name);
		loaded_files[load_name] = data;
		return data;
	}
	
	~File_Data_Handler() {
		for(auto find : loaded_files) free(find.second.data);
	}
};


#endif