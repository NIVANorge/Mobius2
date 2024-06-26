

#ifndef MOBIUS_LEXER_H
#define MOBIUS_LEXER_H


#include "linear_memory.h"
#include "datetime.h"
#include "file_utils.h"
#include "peek_queue.h"
#include "common_types.h"


inline const char *
article(Token_Type type) {
	if(type == Token_Type::unknown || type == Token_Type::identifier || type == Token_Type::integer || type == Token_Type::eof)
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
	return is_numeric_or_bool(type) || type == Token_Type::identifier || type == Token_Type::quoted_string || type == Token_Type::date;
}

struct
Token {
	
	Token() : val_date(), string_value(), type(Token_Type::unknown) {};
	
	Source_Location source_loc;
	String_View     string_value;
	union
	{
		s64       val_int;
		// WARNING: val_double should never be read directly unless you know the type is real. Instead use double_value() below. This is because something that is a valid int could also be interpreted as a double.
		double    val_double;
		u64       val_bool;
		Date_Time val_date;
	};
	Token_Type   type;
	
	double
	double_value() const {
		return (type == Token_Type::real) ? val_double : (double)val_int;
	}
	
	void print_error_location() const;
	void print_error_header() const;
	void print_log_header() const;
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
	
	const char *remainder() {
		return file_data.data + at_char + 1;
	}
	
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

inline Parameter_Value
get_parameter_value(Token *token, Token_Type type) {
	if((type == Token_Type::integer || type == Token_Type::boolean) && token->type == Token_Type::real)
		fatal_error(Mobius_Error::internal, "Invalid use of get_parameter_value().");
	Parameter_Value result;
	if(type == Token_Type::real)
		result.val_real = token->double_value();
	else if(type == Token_Type::integer)
		result.val_integer = token->val_int;
	else if(type == Token_Type::boolean)
		result.val_boolean = token->val_bool;
	else
		fatal_error(Mobius_Error::internal, "Invalid use of get_parameter_value().");
	return result;
}


#endif