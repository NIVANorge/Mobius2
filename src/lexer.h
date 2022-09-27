

#ifndef MOBIUS_LEXER_H
#define MOBIUS_LEXER_H


#include "linear_memory.h"
#include "datetime.h"
#include "file_utils.h"
#include "peek_queue.h"
#include "common_types.h"


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

inline bool
can_be_value_token(Token_Type type) {
	return is_numeric_or_bool(type) || type == Token_Type::identifier || type == Token_Type::quoted_string;
}

struct
Token {
	
	Token() : val_date(), string_value(), type(Token_Type::unknown) {};
	
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
	void print_warning_header();
};

inline bool
is_valid(Token *token) {
	return token && (token->type != Token_Type::unknown) && !(token->type == Token_Type::identifier && token->string_value.count == 0);
}

struct
Token_Stream {
	
	Token_Stream(String_View filename, String_View file_data) : filename(filename), file_data(file_data), line(0), column(0), previous_column(0), allow_date_time_tokens(false), at_char(-1), fold_minus(true) {
		
		//NOTE: In case the file has a BOM (byte order mark), which Notepad tends to add on Windows.
		if(file_data.count >= 3
				&& file_data[0] == (char) 0xEF
				&& file_data[1] == (char) 0xBB
				&& file_data[2] == (char) 0xBF)
			at_char = 2;
	}
	
	Token read_token();
	Token peek_token(s64 peek_at = 0);
	Token expect_token(Token_Type);
	Token expect_token(char);
	
	double       expect_real();
	s64          expect_int();
	bool         expect_bool();
	Date_Time    expect_datetime();
	String_View  expect_quoted_string();
	String_View  expect_identifier();
	
	bool allow_date_time_tokens;
	bool fold_minus;

private:
	String_View filename;
	
	String_View  file_data;
	s64          at_char;
	
	s32 line;
	s32 column;
	s32 previous_column;
	
	Peek_Queue<Token> token_queue;
	
	char peek_char(s64 ahead = 0);
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