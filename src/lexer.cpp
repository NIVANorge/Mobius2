
#include "lexer.h"

#include <limits>

void
Token::print_error_location() {
	error_print("file ", location.filename, " line ", location.line+1, " column ", location.column, ":\n");
}

void
Token::print_error_header() {
	begin_error(Mobius_Error::parsing);
	error_print("In ");
	print_error_location();
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
Token_Stream::peek_char() {
	char c = '\0';
	if(at_char + 1 < file_data.count) c = file_data[at_char + 1];
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
	//TODO: Maybe include [ and ] also.
	return c == '!' || (c >= '%' && c <= '/') || (c >= ':' && c <= '?') || (c >= '{' && c <= '}');
}

inline bool
is_identifier(char c) {
	return isalpha(c) || c == '_';
}

void
Token_Stream::read_token_base(Token *token) {
	*token = {}; // 0-initialize
	
	bool skip_comment = false;
	
	while(true) {
		char c = read_char();
		
		if(skip_comment) { // If we hit a # symbol outside a string token, SkipComment is true, and we continue skipping characters until we hit a newline or end of file.
			if(c == '\n' || c == '\0') skip_comment = false;
			continue;
		}
		
		//NOTE: This is very subtle, but this clause has to be below the check for SkipComment, if we are in a comment we have to check for \n, (and \n isspace and would be skipped by this continue)
		if(isspace(c)) continue; // Always skip whitespace between tokens.
	
	
		token->location.filename = filename;
		token->location.line = line;
		token->location.column = column;
		
		// NOTE: Try to identify the type of the token.
		
		if(c == '\0')                               token->type =  Token_Type::eof;
		else {
			bool is_single = is_single_char_token(c);
			if(c=='-') {
				//peek at the next char to see if it is numeric. In that case parse this as a negative number instead of returning the minus operator.
				char next = peek_char();
				if(isdigit(next))
					is_single = false;
			}
			
			if(is_single)                               token->type = (Token_Type)c;              // NOTE: single-character tokens have type values equal to their char value.
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
}

void
Token_Stream::read_string(Token *token) {
	bool first_quotation_mark = true;
	
	while(true) {
		char c = read_char();
		++token->string_value.count;
		
		if(c == '"') {
			//NOTE: Don't count the quotation marks in the string length or data.
			--token->string_value.count;
			if(first_quotation_mark)
				++token->string_value.data;
			else
				break;
			first_quotation_mark = false;
		} else if (c == '\n') {
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
			// NOTE: in this case we assume the latest read char was the start of another token, so go back one char to make the position correct for the next call to ReadTokenInternal_.
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

inline double
make_double_fast(u64 base, s64 exponent, bool is_negative, bool *success) {
	static const double pow_10[] = {
		1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28, 1e29, 1e30, 1e31, 1e32, 1e33, 1e34, 1e35, 1e36, 1e37, 1e38, 1e39, 1e40, 1e41, 1e42, 1e43, 1e44, 1e45, 1e46, 1e47, 1e48, 1e49, 1e50, 1e51, 1e52, 1e53, 1e54, 1e55, 1e56, 1e57, 1e58, 1e59, 1e60, 1e61, 1e62, 1e63, 1e64, 1e65, 1e66, 1e67, 1e68, 1e69, 1e70, 1e71, 1e72, 1e73, 1e74, 1e75, 1e76, 1e77, 1e78, 1e79, 1e80, 1e81, 1e82, 1e83, 1e84, 1e85, 1e86, 1e87, 1e88, 1e89, 1e90, 1e91, 1e92, 1e93, 1e94, 1e95, 1e96, 1e97, 1e98, 1e99, 1e100, 1e101, 1e102, 1e103, 1e104, 1e105, 1e106, 1e107, 1e108, 1e109, 1e110, 1e111, 1e112, 1e113, 1e114, 1e115, 1e116, 1e117, 1e118, 1e119, 1e120, 1e121, 1e122, 1e123, 1e124, 1e125, 1e126, 1e127, 1e128, 1e129, 1e130, 1e131, 1e132, 1e133, 1e134, 1e135, 1e136, 1e137, 1e138, 1e139, 1e140, 1e141, 1e142, 1e143, 1e144, 1e145, 1e146, 1e147, 1e148, 1e149, 1e150, 1e151, 1e152, 1e153, 1e154, 1e155, 1e156, 1e157, 1e158, 1e159, 1e160, 1e161, 1e162, 1e163, 1e164, 1e165, 1e166, 1e167, 1e168, 1e169, 1e170, 1e171, 1e172, 1e173, 1e174, 1e175, 1e176, 1e177, 1e178, 1e179, 1e180, 1e181, 1e182, 1e183, 1e184, 1e185, 1e186, 1e187, 1e188, 1e189, 1e190, 1e191, 1e192, 1e193, 1e194, 1e195, 1e196, 1e197, 1e198, 1e199, 1e200, 1e201, 1e202, 1e203, 1e204, 1e205, 1e206, 1e207, 1e208, 1e209, 1e210, 1e211, 1e212, 1e213, 1e214, 1e215, 1e216, 1e217, 1e218, 1e219, 1e220, 1e221, 1e222, 1e223, 1e224, 1e225, 1e226, 1e227, 1e228, 1e229, 1e230, 1e231, 1e232, 1e233, 1e234, 1e235, 1e236, 1e237, 1e238, 1e239, 1e240, 1e241, 1e242, 1e243, 1e244, 1e245, 1e246, 1e247, 1e248, 1e249, 1e250, 1e251, 1e252, 1e253, 1e254, 1e255, 1e256, 1e257, 1e258, 1e259, 1e260, 1e261, 1e262, 1e263, 1e264, 1e265, 1e266, 1e267, 1e268, 1e269, 1e270, 1e271, 1e272, 1e273, 1e274, 1e275, 1e276, 1e277, 1e278, 1e279, 1e280, 1e281, 1e282, 1e283, 1e284, 1e285, 1e286, 1e287, 1e288, 1e289, 1e290, 1e291, 1e292, 1e293, 1e294, 1e295, 1e296, 1e297, 1e298, 1e299, 1e300, 1e301, 1e302, 1e303, 1e304, 1e305, 1e306, 1e307, 1e308,
	};
	
	if(exponent < -308 || exponent > 308) {
		*success = false;
		return 0.0;
	}
	*success = true;
	
	// Note: this is the correct approximation if(Exponent >= -22 && Exponent <= 22 && Base <= 9007199254740991)
	// See
	// Clinger WD. How to read floating point numbers accurately.
	// ACM SIGPLAN Notices. 1990	
	
	// For other numbers there is some small error due to rounding of large powers of 10 (in the 15th decimal place).
	// TODO: Replace this with the fast_double_parser library
	// atof is not an option since it is super slow (6 seconds parsing time on some typical input files).

	double result = (double)base;
	if(exponent < 0)
		result /= pow_10[-exponent];
	else
		result *= pow_10[exponent];
	result = is_negative ? -result : result;
	return result;
}

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
					// TODO: Ideally we should use arbitrary precision integers for Base instead, or shift to it if this happens. When parsing doubles, we should allow higher number of digits.
					token->print_error_header();
					fatal_error("Overflow in numeric literal.");
				}
			}
			++numeric_pos;
		}
		else {
			// NOTE: We assume that the latest read char was the start of another token, so go back one char to make the position correct for the next call to ReadTokenInternal_.
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
		token->val_double = make_double_fast(base, signed_exponent, is_negative, &success);
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
			// NOTE: We assume that the latest read char was the start of another token, so go back one char to make the position correct for the next call to ReadTokenInternal_.
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

u64
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

/*
void
Token_Stream::ReadQuotedStringList(std::vector<token_string> &ListOut)
{
	ExpectToken('{');
	while(true)
	{
		token Token = ReadToken();
		
		if(Token.Type == TokenType_QuotedString)
			ListOut.push_back(Token.StringValue);
		else if(Token.Type == '}')
			break;
		else if(Token.Type == TokenType_EOF)
		{
			PrintErrorHeader();
			FatalError("End of file before list was ended.\n");
		}
		else
		{
			PrintErrorHeader();
			FatalError("Unexpected token.\n");
		}
	}
}
*/