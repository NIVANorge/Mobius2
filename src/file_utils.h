

#ifndef MOBIUS_FILE_UTILS_H
#define MOBIUS_FILE_UTILS_H

#include <stdio.h>
#include <locale>
#include <codecvt>

#include "mobius_common.h"
#include "linear_memory.h"

inline FILE *
open_file(String_View filename, String_View mode)
{
	// Wrapper to allow for non-ascii names on Windows. Assumes Filename is UTF8 formatted.
	FILE *file;
#ifdef _WIN32
	std::u16string filename16 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(filename.data, filename.data+filename.count);
	std::u16string mode16     = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(mode.data, mode.data+mode.count);
	file = _wfopen((wchar_t *)filename16.data(), (wchar_t *)mode16.data());
#else
	std::string filename8(filename.data, filename.count);
	std::string mode8    (mode.data, mode.count);
	file = fopen(filename8.data(), mode8.data());
#endif

	if(!file)
		fatal_error(Mobius_Error::file, "Tried to open file \"", filename, "\", but was not able to.");

	return file;
}

inline String_View
read_entire_file(String_View filename)
{
	String_View file_data;
	
	FILE *file = open_file(filename, "rb");

	fseek(file, 0, SEEK_END);
	file_data.count = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	if(file_data.count == 0) {
		fclose(file);
		fatal_error(Mobius_Error::file, "The file ", filename, " is empty.");
	}
	
	char *data = (char *)malloc(file_data.count + 1);
	
	if(!data)
		fatal_error(Mobius_Error::file, "Unable to allocate enough memory to read in file ", filename);

	size_t read_size = fread((void *)data, 1, file_data.count, file); // NOTE: Casting away constness, but it doesn't matter since we just allocated it ourselves.
	fclose(file);
	
	if(read_size != file_data.count) {
		free((void *)data);
		fatal_error(Mobius_Error::file, "Unable to read the entire file ", filename);
	}
	
	data[file_data.count] = '\0';    // Zero-terminate it in case we want to interface with C libraries.
	file_data.data = data;
	
	return file_data;
}


#endif