

#include <stdio.h>
#include <locale>
#include <codecvt>
#include "file_utils.h"

FILE *
open_file(String_View file_name, String_View mode) {
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

	return file;
}

String_View
read_entire_file(String_View file_name, Source_Location from) {
	String_View file_data;
	
	FILE *file = open_file(file_name, "rb");
	if(!file) {
		if(from.type != Source_Location::Type::internal)
			from.print_error_header(Mobius_Error::file);
		fatal_error("Unable to open file \"", file_name, "\".");
	}

	fseek(file, 0, SEEK_END);
	file_data.count = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	if(file_data.count == 0) {
		fclose(file);
		fatal_error(Mobius_Error::file, "The file ", file_name, " is empty.");
	}
	
	char *data = (char *)malloc(file_data.count + 1);
	if(!data)
		fatal_error(Mobius_Error::file, "Unable to allocate enough memory to load file ", file_name);

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

inline bool is_slash(char c) { return c == '\\' || c == '/'; }

bool
is_relative_path(String_View file_name) {
	if(file_name.count == 0) return true;
	if(is_slash(file_name.data[0])) return false;  // TODO: Maybe only on linux
	if(file_name.count >= 3) {
		if(file_name.data[0] >= 'A' && file_name.data[0] <= 'Z' && file_name.data[1] == ':' && is_slash(file_name.data[2]))
			return false;
	}
	return true;
}

String_View
make_path_relative_to(String_View file_name, String_View relative_to) {
	// TODO: don't rewind past a directory name (in that case there should be an error).
	// Would probably be better to tokenize the paths first?
	
	if(!is_relative_path(file_name)) return file_name;
	
	constexpr int maxpath = 1024;  //TODO: Should be system dependent?
	static char new_path[maxpath]; //TODO make a string builder instead?
	
	int pos = 0;
	int last_slash = -1;
	while(pos < relative_to.count) {
		if(pos == maxpath) fatal_error(Mobius_Error::internal, "Oops too long path, make better implementation!");
		char c = relative_to[pos];
		if(is_slash(c)) { last_slash = pos; c = '/'; }
		new_path[pos] = c;
		++pos;
	}
	pos = last_slash + 1;    // Remove the file name from the relative_to path.
	int cursor = 0;
	bool start_dir = true;
	while(cursor < file_name.count) {
		if(pos == maxpath) fatal_error(Mobius_Error::internal, "Oops too long path, make better implementation!");
		char c = file_name[cursor];
		if(start_dir && (cursor+2 < file_name.count)
			&& c == '.'  && file_name[cursor+1] == '.' && is_slash(file_name[cursor+2])) {
			// If the file name that we append now looks like  ../[whatever] , try to rewind one directory in the path if possible before writing out [whatever]
			if(pos >= 2) {
				cursor += 3; // consume the  ../ in the file name.
				pos -= 2; // rewind to right before the last slash we read.
				if(pos < 0) pos = 0;
				if(!(pos >= 2 && new_path[pos] == '.' && new_path[pos-1] == '.' && is_slash(new_path[pos-2]))) {
					// if the path looks like [whatever]/../  just keep the ../ since the file name agrees with it.
					// otherwise if the path looks like [whatever]/ rewind to the beginning of [whatever] (and proceed to overwrite it).
					while(pos >= 0) {
						char c = new_path[pos];
						if(is_slash(c)) { ++pos; break; }
						--pos;
					}
				}
				if(pos < 0) pos = 0;
			} else if (pos == 1) { pos--; cursor += 3; }
		}
		if(cursor >= file_name.count) break;
		c = file_name[cursor];
		if(is_slash(c)) { start_dir = true; c = '/'; }
		else start_dir = false;
		new_path[pos] = c;
		++cursor;
		++pos;
	}
	if(pos == maxpath) fatal_error(Mobius_Error::internal, "Oops too long path, make better implementation!");
	new_path[pos] = '\0';
	
	// To make it work on Linux:
	char *c = &new_path[0];
	while(*c) {
		if(is_slash(*c)) *c = '/';
		c++;
	}
	
	String_View result = new_path;
	return result;
}

bool
bottom_directory_is(String_View path, String_View directory) {
	if(directory.count > path.count) return false;
	for(int idx = 0; idx < directory.count; ++idx)
		if(path[idx] != directory[idx]) return false;
	if(path.count == directory.count || is_slash(path[directory.count]))
		return true;
	return false;
}

String_View
get_extension(String_View file_name, bool *success) {
	int len = file_name.count;
	String_View extension;
	extension.data = file_name.data+(len-1);
	extension.count = 0;
	while(*extension.data != '.' && extension.data != file_name.data) { --extension.data; ++extension.count; }
	*success = !(extension.data == file_name.data && *extension.data != '.');
	return extension;
}


String_View
File_Data_Handler::load_file(String_View file_name, Source_Location from, String_View relative_to, std::string *normalized_path_out) {
	String_View load_name = file_name;
	if(relative_to.count)
		load_name = make_path_relative_to(file_name, relative_to);
	auto find = loaded_files.find(load_name);
	if(find != loaded_files.end()) {
		if(normalized_path_out) *normalized_path_out = find->first;
		return find->second;
	}
	
	String_View data = read_entire_file(load_name, from);
	loaded_files[load_name] = data;
	if(normalized_path_out) *normalized_path_out = load_name;
	
	return data;
}

std::string
standardize_base_path(String_View path) {
	// Note correctness checks happen in user code.
	
	std::string new_path = std::string(path);
	
	if(new_path.empty()) return new_path;
	
	if(!is_slash(new_path.back()))
		new_path += '/';
	
	return new_path;
}