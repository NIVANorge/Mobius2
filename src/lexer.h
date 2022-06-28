

#ifndef MOBIUS_LEXER_H
#define MOBIUS_LEXER_H


#include "linear_memory.h"
#include "datetime.h"
#include "file_utils.h"
#include "peek_queue.h"

enum class Token_Type : char
{
	#define ENUM_VALUE(handle, name) handle,
	#include "token_types.incl"
	#undef ENUM_VALUE
	max_multichar = 20,
};



inline const char *
name(Token_Type type) {
	if(type <= Token_Type::max_multichar)                   
		switch(type) {
			#define ENUM_VALUE(handle, name) case Token_Type::handle: return name;
			#include "token_types.incl"
			#undef ENUM_VALUE
		}
	else {
		static char buf[2] = {0, 0};
		buf[0] = (char)type;
		return buf;
	}
	return "";
}

inline const char *
article(Token_Type type) {
	if(type == Token_Type::unknown || type == Token_Type::identifier || type == Token_Type::eof)
		return "an";
	return "a";
}

inline bool
is_numeric(Token_Type type) {
	return (type == Token_Type::real) || (type == Token_Type::integer);
}

inline bool
is_numeric_or_bool(Token_Type type) {
	return is_numeric(type) || (type == Token_Type::boolean);
}

inline
can_be_value_token(Token_Type type) {
	return is_numeric_or_bool(type) || type == Token_Type::identifier || type == Token_Type::quoted_string;
}

struct
Source_Location {
	String_View filename;
	s32 line, column;
	
	void print_error();
	void print_error_header();
};

struct
Token {
	
	Token() : val_date() {};
	
	Source_Location location;
	String_View     string_value;
	union
	{
		s64       val_int;
		// WARNING: DoubleValue should never be read directly, instead use double_value() below. This is because something that is a valid int could also be interpreted as a double.
		// TODO:    How to enforce that in the compiler without making too much boilerplate for accessing the other values?
		double    val_double;
		u64       val_bool;
		Date_Time val_date;
	};
	Token_Type   type;
	
	double
	double_value() const {
		if(type == Token_Type::real) return val_double;
		return (double)val_int;
	}
	
	void print_error_location();
	void print_error_header();
};

inline bool
is_valid(Token *token) {
	return token && (token->type != Token_Type::unknown) && !(token->type == Token_Type::identifier && token->string_value.count == 0);
}

struct
Token_Stream {
	
	Token_Stream(String_View filename) : filename(filename), line(0), column(0), previous_column(0), allow_date_time_tokens(false) {
		at_char = -1;
		
		file_data = read_entire_file(filename);
		
		//NOTE: In case the file has a BOM (byte order mark), which Notepad tends to do on Windows.
		if(file_data.count >= 3 
				&& file_data[0] == (char) 0xEF
				&& file_data[1] == (char) 0xBB
				&& file_data[2] == (char) 0xBF)
			at_char = 2;
	}
	
	~Token_Stream() {
		if(file_data.data) free((void *)file_data.data); // NOTE: Casting away constness, but it doesn't matter since we just allocated it ourselves.
	}
	
	Token read_token();
	Token peek_token(s64 peek_at = 0);
	Token expect_token(Token_Type);
	Token expect_token(char);
	
	double       expect_real();
	u64          expect_int();
	bool         expect_bool();
	Date_Time    expect_datetime();
	String_View  expect_quoted_string();
	String_View  expect_identifier();
	
	bool allow_date_time_tokens;
	//void ReadQuotedStringList(std::vector<token_string> &ListOut);
	//void ReadParameterSeries(std::vector<parameter_value> &ListOut, const parameter_spec &Spec);

private:
	String_View filename;
	
	String_View  file_data;
	s64          at_char;
	
	s32 line;
	s32 column;
	s32 previous_column;
	
	Peek_Queue<Token> token_queue;
	
	char peek_char();
	char read_char();
	void putback_char();
	
	void read_number(Token *token);
	void read_date_or_time(Token *token, s32 first_part);
	void read_string(Token *token);
	void read_identifier(Token *token);
	void read_token_base(Token *token);
	const Token * peek_token_base(s64 peek_at = 0);
};



#endif