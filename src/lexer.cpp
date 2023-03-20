
#include "lexer.h"

#include <limits>


//NOTE: This is to produce excel-style cell names, like "K32". Doesn't really belong to this file except that this is where we decided to do the print Source_Location implementation.
char *
col_row_to_cell(int col, int row, char *buf) {
	int num_A_Z = 'Z' - 'A' + 1;
	int n_col = col;
	while (n_col > 0) {
		int letter = n_col/num_A_Z;
		if (letter == 0) {
			letter = n_col;
			*buf = char('A' + letter - 1);
			buf++;
			break;
		} else {
			n_col -= letter*num_A_Z;
			*buf = char('A' + letter - 1);
			buf++;
		}
	}
	itoa(row, buf, 10);
	while(*buf != 0) ++buf;
	return buf;
}

void
Source_Location::print_error() const {
	if(type == Type::text_file)
		error_print("file ", filename, " line ", line+1, " column ", column, ":\n");
	else if(type == Type::spreadsheet) {
		static char buf[64];
		col_row_to_cell(column, line, buf);
		error_print("file ", filename, " tab ", tab, " cell ", buf, ":\n");
	} else
		error_print("(compiler internal)\n");
}

void
Source_Location::print_error_header(Mobius_Error type) const {
	begin_error(type);
	error_print("In ");
	print_error();
}

void
Source_Location::print_warning_header() const {
	if(type == Type::text_file)
		warning_print("file ", filename, " line ", line+1, " column ", column, ":\n");
	else if(type == Type::spreadsheet) {
		static char buf[64];
		col_row_to_cell(column, line, buf);
		warning_print("file ", filename, " cell ", buf, ":\n");
	} else
		warning_print("(compiler internal)\n");
}

void
Token::print_error_location() {
	source_loc.print_error();
}

void
Token::print_error_header() {
	source_loc.print_error_header();
}

void
Token::print_warning_header() {
	source_loc.print_warning_header();
}

const Token *
Token_Stream::peek_token_base(s64 peek_at) {
	if(peek_at < 0)
		fatal_error(Mobius_Error::internal, "Tried to peek backwards on already consumed tokens when parsing a file.");
	
	token_queue.reserve(peek_at+1);
	while(token_queue.max_peek() < peek_at) {
		Token *token = token_queue.append();
		read_token_base(token);
	}
	
	return token_queue.peek(peek_at);
}

Token
Token_Stream::peek_token(s64 peek_at) {
	return *peek_token_base(peek_at);
}

Token
Token_Stream::read_token() {
	const Token *token = peek_token_base();
	token_queue.advance();
	return *token;
}

Token
Token_Stream::expect_token(Token_Type type) {
	Token token = read_token();
	if(token.type != type) {
		token.print_error_header();
		error_print("Expected a token of type ", name(type), ", got ", article(token.type), " ", name(token.type));
		//error_print("Expected a token of type ", (int)type, ", got ", article(token.type), " ", (int)token.type);
		if(token.type == Token_Type::quoted_string || token.type == Token_Type::identifier)
			error_print(" \"", token.string_value, "\".");
		mobius_error_exit();
	}
	return token;
}

Token
Token_Stream::expect_token(char type) {
	return expect_token((Token_Type)type);
}

inline int
col_width(char c) {
	if(c == '\t') return 4;
	return (((u8)c >> 7) == 0 || ((u8)c >> 6) == 0b11);  // A utf-8 char sequence starts with either 0xxxxxxx or 11xxxxxxx. Just count the first char in the sequence.
}


inline String_View 
get_utf8(char *s) {
	//TODO: ideally should check for misformatting...
	String_View result = {};
	result.data = s;
	result.count = 1;
	char c = *s;
	if(((u8)c >> 7) == 0) {
		result.count = 1;
	} else {
		for(int pos = 1; pos < 4; ++pos) {
			char c = s[pos];
			if(((u8)c >> 6) != 0b10) break;
			result.count = pos + 1;
		}
	}
	return result;
}

char
Token_Stream::peek_char(s64 ahead) {
	char c = '\0';
	if(at_char + ahead + 1 < file_data.count) c = file_data[at_char + 1 + ahead];
	return c;
}

char
Token_Stream::read_char() {
	++at_char;
	char c = '\0';
	if(at_char < file_data.count) c = file_data[at_char];
	
	if(c == '\n') {
		++line;
		previous_column = column;
		column = 0;
	}
	else
		column += col_width(c);
	return c;
}

void
Token_Stream::putback_char() {
	// If we are at the end, there is no point in putting back characters, because we are not reading more tokens any way.
	if(at_char == file_data.count) return;
	
	char c = file_data[at_char];
	if(c == '\n') {
		--line;
		column = previous_column;
	}
	else
		column -= col_width(c);
	
	--at_char;
}

inline bool
is_single_char_token(char c) {
	return c == '!' || (c >= '%' && c <= '/') || (c >= ':' && c <= '?') || (c >= '{' && c <= '}') || c == '^' || c=='[' || c==']';
}

inline bool
is_identifier(char c) {
	return isalpha(c) || c == '_';
}

inline bool
is_space(char c) {
	//return isspace(c);
	// NOTE: MSVC implementation of isspace crashes on a non-ascii byte, so we have to make our
	// own.
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void
Token_Stream::read_token_base(Token *token) {
	*token = {}; // 0-initialize
	
	bool skip_comment = false;
	bool skip_multiline = false;
	
	token->source_loc.filename = filename;
	token->source_loc.type = Source_Location::Type::text_file;
	
	while(true) {
		char c = read_char();
		char n = peek_char();
		
		if(skip_comment) { // If we hit a # symbol outside a string token, skip_comment is true, and we continue skipping characters until we hit a newline or end of file.
			if(c == '\n' || c == '\0') skip_comment = false;
			continue;
		}
		
		if(skip_multiline) {
			if(c == '*' && n == '/') {
				read_char(); // Consume the '/'
				skip_multiline = false;
			}
			continue;
		}
		
		//NOTE: This is a bit subtle, but this clause has to be below the check for skip_comment, if we are in a comment we have to check for \n, (and \n isspace and would be skipped by this continue)
		if(is_space(c)) continue; // Always skip whitespace between tokens.

		token->source_loc.line = line;
		token->source_loc.column = column;
		
		// NOTE: Try to identify the type of the token.
		
		if(c == '\0')                               token->type =  Token_Type::eof;
		else {
			bool is_single = is_single_char_token(c);
			if((fold_minus && c == '-') || c == '.') {
				//peek at the next char to see if it is numeric. In that case parse this as a number instead of returning the minus or dot.
				//char next = peek_char();
				if(isdigit(n))
					is_single = false;
			}
			
			if(c == '/' && n == '*') {
				skip_multiline = true;
				continue;
			} else if(is_single) {
				token->type = (Token_Type)c;              // NOTE: single-character tokens have type values equal to their char value.
			
				char nn = peek_char(1);
				if     (c == '<' && n == '=') token->type = Token_Type::leq;
				else if(c == '>' && n == '=') token->type = Token_Type::geq;
				else if(c == '!' && n == '=') token->type = Token_Type::neq;
				else if(c == ':' && n == '=') token->type = Token_Type::def;
				else if(c == '-' && n == '>') token->type = Token_Type::arr_r;
				else if(c == '=' && n == '>') token->type = Token_Type::d_arr_r;
				
				if     (c == '-' && n == '>' && nn == '>') token->type = Token_Type::arr_r_r;
				else if(c == '=' && n == '>' && nn == '>') token->type = Token_Type::d_arr_r_r;
			}
			else if(c == '"')                           token->type =  Token_Type::quoted_string;
			else if(c == '-' || c == '.' || isdigit(c)) token->type =  Token_Type::real;          // NOTE: Can change type to integer, date or time when parsing it below
			else if(is_identifier(c))                   token->type =  Token_Type::identifier;   // NOTE: Can change type to bool or real when parsing it below
			else if(c == '#') {
				skip_comment = true;
				continue;
			} else {
				token->print_error_header();
				fatal_error("Found a token of unknown type, starting with: ", get_utf8(&file_data[at_char]), ".");
			}
		}
		
		token->string_value = file_data.substring(at_char, 0); // We don't know how long it will be yet, the length will be adjusted as we parse
		break;
	}
	
	// TODO: It is not that easy to follow the logic of this function :(

	if(token->type > Token_Type::max_multichar) { //NOTE: We have a single-character token.
		token->string_value.count = 1;
		return;
	}

	putback_char();  // Put the char back so that we can start processing it afresh below.
	
	// NOTE: Continue processing multi-character tokens:
	
	if(token->type == Token_Type::quoted_string)
		read_string(token);
	else if(token->type == Token_Type::identifier)
		read_identifier(token);
	else if(token->type == Token_Type::real)
		read_number(token);
	else {  // we have a multi-character operator
		read_char(); read_char(); // consume the two chars.
		token->string_value.count = 2;
		if(token->type == Token_Type::arr_r_r || token->type == Token_Type::d_arr_r_r) {
			read_char();
			token->string_value.count = 3;
		}
		return;
	}
}

void
Token_Stream::read_string(Token *token) {
	bool docstring = false;
	
	token->string_value.count = 0;
	char c = read_char(); // This is just the initial quotation mark that we already detected.
	++token->string_value.data; // don't store the quotation mark in the string data.
	
	if(peek_char(0) == '"' && peek_char(1) == '"') {
		docstring = true;
		read_char(); read_char();
		token->string_value.data += 2;
		c = peek_char();
		if(c == '\r') { //Skip initial carriage return
			read_char();
			++token->string_value.data;
			c = peek_char();
		}
		if(c == '\n') { //Skip initial newline.
			read_char();
			++token->string_value.data;
		}
	}
	
	//TODO: for docstrings we would ideally like to trim tabs per line up to the column position of where it started, but that requires us to copy the string data into another location.
	
	while(true) {
		char c = read_char();
		++token->string_value.count;
		
		if(c == '"') {
			bool close = true;
			if(docstring) {
				// determine if this is actually closing the string.
				if(peek_char(0) == '"' && peek_char(1) == '"') {
					read_char(); read_char();
					--token->string_value.count;
					if(token->string_value[token->string_value.count-1] == '\n')
						--token->string_value.count;  // Trim away closing newline right before """ if it exists.
					if(token->string_value[token->string_value.count-1] == '\r')
						--token->string_value.count; // Trim away carriage return if it exists.
					break;
				}
			} else {
				// Don't count the quotation marks in the string value.
				--token->string_value.count;
				break;
			}
		} else if (c == '\n' && !docstring) {
			token->print_error_header();
			fatal_error("New line before quoted string was closed.");
		} else if (c == '\0') {
			token->print_error_header();
			fatal_error("End of file before quoted string was closed.\n");
		}
	}
}

void
Token_Stream::read_identifier(Token *token) {
	
	while(true) {
		char c = read_char();
		++token->string_value.count;
		
		if(!is_identifier(c) && !isdigit(c)) {
			// NOTE: in this case we assume the latest read char was the start of another token, so go back one char to make the position correct for the next call to read_token.
			putback_char();
			--token->string_value.count;
			break;
		}
	}
	
	// Check for reserved identifiers that are actually a different token type.
	if(token->string_value == "true") {
		token->type = Token_Type::boolean;
		token->val_bool = true;
	} else if(token->string_value == "false") {
		token->type = Token_Type::boolean;
		token->val_bool = false;
	} else if(token->string_value == "NaN" || token->string_value == "nan" || token->string_value == "Nan") {
		token->type = Token_Type::real;
		token->val_double = std::numeric_limits<double>::quiet_NaN();
	}
}

template<typename N> inline bool
append_digit(N *number, char digit) {
	N addendand = (N)(digit - '0');
	constexpr N max_value = std::numeric_limits<N>::max();
	if( (max_value - addendand) / 10  < *number) return false;
	*number = *number * 10 + addendand;
	return true;
}

#include "../third_party/fast_double_parser/fast_double_parser.h"

void
Token_Stream::read_number(Token *token) {
	s32 numeric_pos = 0;
	
	bool is_negative  = false;
	bool has_comma    = false;
	bool has_exponent = false;
	bool exponent_is_negative = false;
	u64 base               = 0;
	u64 exponent           = 0;
	s32 digits_after_comma = 0;
	
	while(true) {
		char c = read_char();
		++token->string_value.count;
		
		if(c == '-') {
			if( (has_comma && !has_exponent) || (is_negative && !has_exponent) || numeric_pos != 0) {
				if(allow_date_time_tokens) {
					if(!has_comma && !has_exponent) {
						//NOTE we have encountered something of the form x- where x is a plain number. Assume it continues on as x-y-z, i.e. this is a date.
						token->type = Token_Type::date;
						s32 first_part = (s32)base;
						if(is_negative) first_part = -first_part; //Years could be negative (though I doubt that will be used in practice).
						read_date_or_time(token, first_part);
						return;
					} else {
						token->print_error_header();
						fatal_error("Misplaced minus in numeric literal.");
					}
				} else {
					putback_char();
					--token->string_value.count;
					break;
				}
			} else {
				if(has_exponent) {
					if(exponent_is_negative) {
						token->print_error_header();
						fatal_error("Double minus sign in exponent of numeric literal.");
					}
					exponent_is_negative = true;
				} else {
					if(is_negative) {
						token->print_error_header();
						fatal_error("Double minus sign in numeric literal.");
					}
					is_negative = true;
				}
				numeric_pos = 0;
			}
		} else if(c == ':') {
			if (allow_date_time_tokens) {
				if(has_comma || has_exponent || is_negative) {
					token->print_error_header();
					fatal_error("Mixing numeric notation with time notation.");
				}
				//NOTE we have encountered something of the form x: where x is a plain number. Assume it continues on as x:y:z, i.e. this is a time.
				token->type = Token_Type::time;
				s32 first_part = (s32)base;
				read_date_or_time(token, first_part);
				return;
			} else {
				putback_char();
				--token->string_value.count;
				break;
			}
		} else if(c == '+' && (has_exponent && numeric_pos == 0)) {
			// We allow for expressions of the type 1e+5 . The + doesn't do anything, so there is no code here.
		} else if(c == '.')	{
			if(has_exponent) {
				token->print_error_header();
				fatal_error("Decimal separator in exponent in numeric literal.");
			}
			if(has_comma) {
				token->print_error_header();
				fatal_error("More than one decimal separator in a numeric literal.");
			}
			numeric_pos = 0;
			has_comma = true;
		} else if(c == 'e' || c == 'E') {
			if(has_exponent) {
				token->print_error_header();
				fatal_error("More than one exponent sign ('e' or 'E') in a numeric literal.");
			}
			numeric_pos = 0;
			has_exponent = true;
		} else if(isdigit(c)) {
			if(has_exponent)
				append_digit<u64>(&exponent, c); //TODO: error check for overflow here too?
			else {
				if(has_comma)
					digits_after_comma++;

				if(!append_digit<u64>(&base, c)){
					// TODO: Ideally we should use arbitrary precision integers for base instead, or shift to it if this happens. When parsing doubles, we should allow higher number of digits.
					token->print_error_header();
					fatal_error("Overflow in numeric literal.");
				}
			}
			++numeric_pos;
		}
		else {
			// NOTE: We assume that the latest read char was the start of another token, so go back one char to make the position correct for the next call to read_token.
			putback_char();
			--token->string_value.count;
			break;
		}
	}
	
	if(!has_comma && !has_exponent) {
		token->type = Token_Type::integer;
		if(base > std::numeric_limits<s64>::max()) {
			token->print_error_header();
			fatal_error("Overflow in numeric literal.");
		}
		token->val_int = is_negative ? -(s64)base : (s64)base;
	} else {
		s64 signed_exponent = exponent_is_negative ? -(s64)exponent : (s64)exponent;
		signed_exponent -= digits_after_comma;
		bool success;
		//token->val_double = make_double_fast(signed_exponent, base, is_negative, &success);
		token->val_double = fast_double_parser::compute_float_64(signed_exponent, base, is_negative, &success);
		if(!success) {
			token->print_error_header();
			fatal_error("Overflow in numeric literal.");
		}
	}
}

void
Token_Stream::read_date_or_time(Token *token, s32 first_part) {
	char separator = '-';
	const char *format = "YYYY-MM-DD";
	if(token->type == Token_Type::time) {
		separator = ':';
		format = "hh:mm:ss";
	}
	
	s32 date_pos = 1;
	s32 date[3] = {first_part, 0, 0};
	
	while(true) {
		char c = read_char();
		++token->string_value.count;	
			
		if(c == separator) {
			++date_pos;
			if(date_pos == 3) {
				token->print_error_header();
				fatal_error("Too many '", separator, "' signs in date or time literal.");
				// or we could assume this is the beginning of a new token, i.e. "1997-8-1-5" is interpreted as the two tokens "1997-8-1" "-5". Don't know what is best.
			}
		} else if(isdigit(c)) {
			if(!append_digit<s32>(&date[date_pos], c)) {
				token->print_error_header();
				fatal_error("Overflow in numeric literal.");
			}
		}
		else {
			// NOTE: We assume that the latest read char was the start of another token, so go back one char to make the position correct for the next call to read_token.
			putback_char();
			--token->string_value.count;
			break;
		}
	}
	
	if(date_pos != 2) {
		token->print_error_header();
		fatal_error("Invalid ", name(token->type), " literal. It must be on the form ", format, ".");
	}
	bool success;
	if(token->type == Token_Type::date)
		token->val_date = Date_Time(date[0], date[1], date[2], &success);
	else {
		token->val_date = Date_Time();
		success = token->val_date.add_timestamp(date[0], date[1], date[2]);
	}
	
	if(!success) {
		token->print_error_header();
		fatal_error("The ", name(token->type), " ", token->string_value, " does not exist.");
	}
}


double
Token_Stream::expect_real() {
	Token token = read_token();
	bool is_negative = false;
	if((char)token.type == '-') {
		is_negative = true;
		token = read_token();
	}
	if(!is_numeric(token.type)) {
		token.print_error_header();
		fatal_error("Expected a number, got ", article(token.type), " ", name(token.type), ".");
	}
	double value = token.double_value();
	value = is_negative ? -value : value;
	return value;
}

s64
Token_Stream::expect_int() {
	Token token = expect_token(Token_Type::integer);
	return token.val_int;
}

bool
Token_Stream::expect_bool() {
	Token token = expect_token(Token_Type::boolean);
	return token.val_bool;
}

Date_Time
Token_Stream::expect_datetime() {
	Token date_token = expect_token(Token_Type::date);
	Token potential_time_token = peek_token();
	if(potential_time_token.type == Token_Type::time) {
		read_token();
		date_token.val_date += potential_time_token.val_date;
	}
	return date_token.val_date;
}

String_View
Token_Stream::expect_quoted_string() {
	Token token = expect_token(Token_Type::quoted_string);
	return token.string_value;
}

String_View
Token_Stream::expect_identifier() {
	Token token = expect_token(Token_Type::identifier);
	return token.string_value;
}
