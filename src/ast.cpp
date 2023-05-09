
#include <unordered_set>

#include "ast.h"

/*
	The basis for any Mobius2 model specification is a "declaration". Any source file contains a sequence of declarations, and each of these can have sub-declarations.
	
	This file contains functionality to take a token stream and parse it as a declaration (possibly recursively).
	
	The general form of a declaration is
	
		identifier : decl_type(arg, ...) @note_1 { body_1 } ... @note_n { body_n }
	
	The first identifier is the "handle" to the declaration, and can be used to refer to the object created by the declaration in other parts of the code.
	In some cases you can make a declaration without a handle.
	
	The decl_type is an identifier, the full list of allowed decl types are in decl_types.incl .
	
	Not all declarations need to have an argument list.
	
	There are 4 types of arguments:
		- value : Something that is a valid parameter value, for instance
			0.1, true, -5,
			or a quoted string "Hello".
		- identifier (chain). Either a single identifier, or a .-separated chain of identifiers.
		- decl  -  Another declaration as an argument to this one.
	If the declaration has one quoted string argument (and possibly other arguments), that string is the "name" of the declaration.
	
	
	Not all declarations have bodies.
	Bodies can have 0 or 1 note
	There are two overarching types of bodies, and which one is used depends on the decl_type
		Decl body:
			This contains just a sequence of other declarations. It can also contain one quoted string, called the docstring of the body.
		Function body:
			This contains a mathematical expression. This should be given separate documentation.
	
	Unit declarations have their entirely separate syntax [a b c, d e f, ...], but are internally handled as a decl where a b c is the "chain" of the first argument, d e f the second. Unit arguments are generally on the form
		si_prefix unit_symbol number . e.g.
			k m 2
		which means square kilometers. The si_prefix and number can be omitted if not needed. This should be given separate documentation.
	
	Examples
	
	module("Snow", version(0, 0, 1),
		air  : compartment,
		soil : compartment,
		temp : property
	) {
		
		
		par_group("Snow parameters", soil) {
			ddf_melt   : par_real("Degree-day snow melt factor", [m m, deg_c-1], 0.1, 0, 2)
		}
		
		pot_melt : property("Potential snow melt")
		
		var(soil.pot_melt, [m m, day-1]) {
			air.temp * ddf_melt
		}
	}
*/


Argument_AST::~Argument_AST() { delete decl; }
Function_Body_AST::~Function_Body_AST() { delete block; }
Regex_Body_AST::~Regex_Body_AST() { delete expr; }


inline bool
is_accepted_for_chain(Token_Type type, bool identifier_only, bool allow_slash) {
	return (identifier_only && type == Token_Type::identifier) || (!identifier_only && (can_be_value_token(type) || (char)type == '/'));
}

void
read_chain(Token_Stream *stream, char separator, std::vector<Token> *list_out, bool identifier_only = true, bool allow_slash = false) {
	while(true) {
		Token token = stream->read_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			fatal_error("End of file while parsing identifier chain.");  //TODO: give location of where it started.
		}
		if(!is_accepted_for_chain(token.type, identifier_only, allow_slash)) {
			token.print_error_header();
			fatal_error("Misformatted chain: \"", token.string_value, "\".");
		}
		list_out->push_back(token);
		token = stream->peek_token();
		if(separator == ' ') {
			if(!is_accepted_for_chain(token.type, identifier_only, allow_slash)) break;
		} else {
			if((char)token.type == separator)
				stream->read_token();
			else
				break;
		}
	}
}

Decl_Type
get_decl_type(Token *string_name, Body_Type *body_type_out) {
	#define ENUM_VALUE(name, body_type, _) if(string_name->string_value == #name) { *body_type_out = Body_Type::body_type; return Decl_Type::name; }
	#include "decl_types.incl"
	#undef ENUM_VALUE
	
	string_name->print_error_header();
	fatal_error("Unrecognized declaration type '", string_name->string_value, "'.");
	
	return Decl_Type::unrecognized;
}

Body_Type
get_body_type(Decl_Type decl_type) {
	#define ENUM_VALUE(name, body_type, _) if(decl_type == Decl_Type::name) { return Body_Type::body_type; }
	#include "decl_types.incl"
	#undef ENUM_VALUE
	return Body_Type::none;
}

void
parse_unit_decl(Token_Stream *stream, Decl_AST *decl) {
	Token token = stream->read_token(); // This is the '['
	decl->source_loc = token.source_loc;
	decl->type = Decl_Type::unit;
	
	while(true) {
		auto peek = stream->peek_token();
		if(peek.type == Token_Type::eof) {
			peek.print_error_header();
			fatal_error("End of file before closing unit declaration");
		} else if((char)peek.type == ']') {
			stream->read_token();
			break;
		} else if(can_be_value_token(peek.type)) {
			auto arg = new Argument_AST();
			read_chain(stream, ' ', &arg->chain, false, true);
			decl->args.push_back(arg);
			
			auto next = stream->peek_token();
			if((char)next.type == ',')
				stream->read_token();
			else if((char)next.type != ']') {
				next.print_error_header();
				fatal_error("Expected a ] or a ,");
			}
		} else {
			peek.print_error_header();
			fatal_error("Misformatted unit declaration.");
		}
	}
}

Decl_AST *
parse_decl_header(Token_Stream *stream, Body_Type *body_type_out) {
	Decl_AST *decl = new Decl_AST();
	
	Token next  = stream->peek_token(1);
	if((char)next.type == ':') {
		decl->handle_name = stream->expect_token(Token_Type::identifier);
		stream->read_token(); // reads the ':'
	}
	
	Token decl_type;
	
	next = stream->peek_token();
	if(next.type == Token_Type::identifier) {
		decl_type = stream->read_token();
	} else if ((char)next.type == '[') {
		parse_unit_decl(stream, decl);
		if(body_type_out)
			*body_type_out = Body_Type::none;
		return decl;
	} else {
		next.print_error_header();
		fatal_error("Unexpected token: ", next.string_value, " .");
	}
	
	// We generally have something on the form a.b.type(bla) . The chain is now {a, b, type}, but we want to store the type separately from the rest of the chain.
	decl->source_loc = decl_type.source_loc;
	
	Body_Type body_type;
	decl->type = get_decl_type(&decl_type, &body_type);
	
	if(decl->type == Decl_Type::unit) {
		next.source_loc.print_error_header();
		fatal_error("Direct unit() declarations are not allowed. Use the [] format instead.");
	}
	
	if(body_type_out)
		*body_type_out = body_type;
	
	next = stream->peek_token();
	if((char)next.type != '(')
		return decl;

	stream->read_token(); // Consume the '('
		
	while(true) {
		next = stream->peek_token();
		
		if((char)next.type == ')') {
			stream->read_token();
			break;
		} else if(can_be_value_token(next.type) || (char)next.type == '[') {
			Argument_AST *arg = new Argument_AST();
			
			Token peek = stream->peek_token(1);
			
			if(next.type == Token_Type::identifier) { // Identifier chain, declaration
				if((char)peek.type == '(' || (char)peek.type == ':')
					arg->decl = parse_decl(stream);
				else {
					read_chain(stream, '.', &arg->chain);
					next = stream->peek_token();
					if((char)next.type == '[') { // Bracketed var location, e.g. layer.water[vertical.top]
						stream->read_token();
						read_chain(stream, '.', &arg->bracketed_chain);
						stream->expect_token(']');
					}
				}
			} else if(next.type == Token_Type::quoted_string || is_numeric_or_bool(next.type)) { // Literal values.
				arg->chain.push_back(next);
				stream->read_token();
			} else if ((char)next.type == '[') { // Unit declaration
				arg->decl = parse_decl(stream);
			}
			
			decl->args.push_back(arg);
			
			next = stream->peek_token();
			if((char)next.type == ',')
				stream->read_token();
			else if((char)next.type != ')') {
				next.print_error_header();
				fatal_error("Expected a ) or a ,");
			}
		} else {
			next.print_error_header();
			fatal_error("Misformatted declaration argument list."); //TODO: better error message.
		}
	}
	
	return decl;
}

Decl_AST *
parse_decl(Token_Stream *stream) {
	
	Body_Type body_type;
	Decl_AST *decl = parse_decl_header(stream, &body_type);

	while(true) {
		Token next = stream->peek_token();
		char ch = (char)next.type;
		if(ch == '@' || ch == '{') {
			stream->read_token();
			Body_AST *body;
			
			if(body_type == Body_Type::decl) {
				body = new Decl_Body_AST();
			} else if(body_type == Body_Type::function) {
				body = new Function_Body_AST();
			} else if(body_type == Body_Type::regex) {
				body = new Regex_Body_AST();
			} else if(body_type == Body_Type::none) {
				next.print_error_header();
				fatal_error("Declarations of type '", name(decl->type), "' can't have declaration bodies.");
			}
			body->opens_at = next.source_loc;
			
			if(ch == '@') {
				body->note = stream->peek_token();
				stream->expect_identifier();
				next = stream->read_token();
			}
			
			if((char)next.type != '{') {
				next.print_error_header();
				fatal_error("Expected a {}-enclosed body for the declaration.");
			}
			
			if(body_type == Body_Type::decl) {
				auto decl_body = static_cast<Decl_Body_AST *>(body);
				while(true) {
					Token token = stream->peek_token();
					if(token.type == Token_Type::quoted_string) {
						stream->read_token();
						if(is_valid(&decl_body->doc_string)) {
							// we already found one earlier.
							token.print_error_header();
							fatal_error("Multiple doc strings for declaration.");
						}
						decl_body->doc_string = token;
					} else if (token.type == Token_Type::identifier) {
						Decl_AST *child_decl = parse_decl(stream);
						decl_body->child_decls.push_back(child_decl);
					} else if ((char)token.type == '}') {
						stream->read_token();
						break;
					} else {
						token.print_error_header();
						fatal_error("Expected a doc string, a declaration, or a }");
					}
				}
			} else if(body_type == Body_Type::function) {
				auto function_body = static_cast<Function_Body_AST *>(body);
				
				// Note: fold_minus=false causes e.g. -1 to be interpreted as two tokens '-' and '1' so that a-1 is an operation rather than just an identifier followed by a number.
				stream->fold_minus = false;
				function_body->block = parse_math_block(stream, next.source_loc);
				stream->fold_minus = true;
			} else if(body_type == Body_Type::regex) {
				auto regex_body = static_cast<Regex_Body_AST *>(body);
				regex_body->expr = parse_regex_list(stream, next.source_loc, true);
			}
			
			decl->bodies.push_back(body);
			
		} else
			break;
	}
	
	return decl;
}


Function_Call_AST *
parse_function_call(Token_Stream *stream) {
	Function_Call_AST *function = new Function_Call_AST();
	
	function->name = stream->read_token();
	function->source_loc = function->name.source_loc;
	stream->read_token(); // consume the '('
	while(true) {
		Token token = stream->peek_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			error_print("End of file while parsing function argument list for function '", function->name.string_value, "' starting at:\n");
			function->name.print_error_location();
			mobius_error_exit();
		} else if((char)token.type == ')') {
			stream->read_token();
			break;
		}
		
		Math_Expr_AST *expr = parse_math_expr(stream);
		function->exprs.push_back(expr);
		
		token = stream->peek_token();
		if((char)token.type == ',')
			stream->read_token();
		else if((char)token.type != ')') {
			token.print_error_header();
			fatal_error("Expected a , or ) .");
		}
	}
	
	return function;
}

int
operator_precedence(Token_Type t) {
	char c = (char)t;
	
	if(c == '|') return 1000;
	else if(c == '&') return 2000;
	else if((c == '<') || (c == '>') || (t == Token_Type::leq) || (t == Token_Type::geq) || (c == '=') || (t == Token_Type::neq)) return 3000;
	else if((c == '+') || (c == '-')) return 4000;
	else if(c == '/') return 5000;
	else if(c == '*' || c == '%') return 6000;   //not sure if * should be higher than /
	else if(c == '^') return 7000;
	
	return 0;
}

int
find_binary_operator(Token_Stream *stream, Token_Type *t) {
	// The number returned is the operator precedence. High means that it has a higher precedence.
	Token peek = stream->peek_token();
	*t = peek.type;
	
	return operator_precedence(*t);
}

Math_Expr_AST *
potentially_parse_unit_conversion(Token_Stream *stream, Math_Expr_AST *lhs, bool expect_operator = true);

Math_Expr_AST *
potentially_parse_binary_operation_rhs(Token_Stream *stream, int prev_prec, Math_Expr_AST *lhs) {
	
	while(true) {
		// TODO: This is not clean...
		if(prev_prec < 5000)
			lhs = potentially_parse_unit_conversion(stream, lhs);
		
		Token_Type oper;
		int cur_prec = find_binary_operator(stream, &oper);
		if(!cur_prec || (cur_prec < prev_prec))
			return lhs;
		
		Token token = stream->read_token(); // consume the operator
		Math_Expr_AST *rhs = parse_primary_expr(stream);
		
		// TODO: This is not clean...
		if(cur_prec < 5000)
			rhs = potentially_parse_unit_conversion(stream, rhs);
		
		Token_Type oper_next;
		int next_prec = find_binary_operator(stream, &oper_next);
		if(next_prec && (cur_prec < next_prec))
			rhs = potentially_parse_binary_operation_rhs(stream, cur_prec + 1, rhs);
		
		Binary_Operator_AST *binop = new Binary_Operator_AST();
		binop->oper = oper;
		binop->exprs.push_back(lhs);
		binop->exprs.push_back(rhs);
		binop->source_loc = token.source_loc;
		lhs = binop;
	}
}

Math_Expr_AST *
potentially_parse_unit_conversion(Token_Stream *stream, Math_Expr_AST *lhs, bool expect_operator) {
	
	bool auto_convert = false;
	bool force        = false;
	bool convert      = false;
	
	auto peek = stream->peek_token();
	if(expect_operator) {
		if(peek.type == Token_Type::arr_r) convert = true;
		else if(peek.type == Token_Type::arr_r_r) { convert = true; auto_convert = true; }
		else if(peek.type == Token_Type::d_arr_r) { convert = true; force = true; }
		else if(peek.type == Token_Type::d_arr_r_r) { convert = true; auto_convert = true; force = true; }
		if(convert)
			stream->read_token();
	} else if((char)peek.type == '[') {
		force = true;
		convert = true;
	}
	
	if(!convert)
		return lhs;
	
	auto unit_conv = new Unit_Convert_AST();
	unit_conv->force = force;
	unit_conv->auto_convert = auto_convert;
	unit_conv->source_loc = peek.source_loc;
	unit_conv->exprs.push_back(lhs);
	if(!auto_convert) {
		auto peek = stream->peek_token();
		if((char)peek.type != '[') {     // TODO: We should also allow referencing units by identifiers!
			peek.print_error_header();
			fatal_error("Expected a unit declaration, starting with '['");
		}
		unit_conv->unit = new Decl_AST();
		stream->fold_minus = true;   // Fold e.g. -1 as a single token instead of two tokens - and 1 .
		parse_unit_decl(stream, unit_conv->unit);
		stream->fold_minus = false;
	}
	
	return unit_conv;// potentially_parse_binary_operation_rhs(stream, 0, unit_conv);
}

Math_Expr_AST *
parse_math_expr(Token_Stream *stream) {
	auto lhs = parse_primary_expr(stream);
	return potentially_parse_binary_operation_rhs(stream, 0, lhs);
	//return potentially_parse_unit_conversion(stream, expr);
}
	
Math_Expr_AST *
parse_primary_expr(Token_Stream *stream) {
	Math_Expr_AST  *result = nullptr;
	Token token = stream->peek_token();
	
	if((char)token.type == '-' || (char)token.type == '!') {
		Source_Location source_loc = token.source_loc;
		stream->read_token();
		auto unary = new Unary_Operator_AST();
		unary->oper = token.type;
		unary->exprs.push_back(parse_primary_expr(stream));
		unary->source_loc = source_loc;
		result = unary;
	} else if((char)token.type == '{') {
		stream->read_token();
		result = parse_math_block(stream, token.source_loc);
	} else if (token.type == Token_Type::identifier) {
		Token peek = stream->peek_token(1);
		if((char)peek.type == '(') {
			result = parse_function_call(stream);
		} else {
			auto val = new Identifier_Chain_AST();
			result = val;
			val->source_loc = token.source_loc;
			read_chain(stream, '.', &val->chain);
			peek = stream->peek_token();
			if((char)peek.type == '[') {
				stream->read_token();
				read_chain(stream, '.', &val->bracketed_chain);
				stream->expect_token(']');
			}
		}
	} else if (is_numeric_or_bool(token.type)) {
		auto val = new Literal_AST();
		val->source_loc = token.source_loc;
		stream->read_token();
		val->value = token;
		result = potentially_parse_unit_conversion(stream, val, false);
	} else if ((char)token.type == '(') {
		stream->read_token();
		result = parse_math_expr(stream);
		Token token = stream->read_token();
		if((char)token.type != ')') {
			token.print_error_header();
			fatal_error("Expected a ')'.");
		}
	} else {
		token.print_error_header();
		fatal_error("Unexpected token ", token.string_value);
	}
	
	return result;
}

Math_Expr_AST *
parse_potential_if_expr(Token_Stream *stream) {
	Math_Expr_AST *value = parse_math_expr(stream);
	Token token = stream->peek_token();
	if(token.type == Token_Type::identifier && token.string_value == "if") {
		Source_Location source_loc = token.source_loc;
		stream->read_token(); // consume the "if"
		
		Math_Expr_AST *condition = parse_math_expr(stream);
		
		auto if_expr = new If_Expr_AST();
		if_expr->source_loc = source_loc;
		if_expr->exprs.push_back(value);
		if_expr->exprs.push_back(condition);
		
		while(true) {
			stream->expect_token(',');
			
			value = parse_math_expr(stream);
			token = stream->read_token();
			
			if(token.type == Token_Type::identifier) {
				if(token.string_value == "if") {
					condition = parse_math_expr(stream);
					if_expr->exprs.push_back(value);
					if_expr->exprs.push_back(condition);
					continue;
				} else if (token.string_value == "otherwise") {
					if_expr->exprs.push_back(value);
					break;
				}
			}
			
			token.print_error_header();
			fatal_error("Expected an 'if' or an 'otherwise'.");
		}
		
		return if_expr;
	}
	return value;
}

Math_Block_AST *
parse_math_block(Token_Stream *stream, Source_Location opens_at) {
	auto block = new Math_Block_AST();
	block->source_loc = opens_at;
	
	while(true) {
		Token token = stream->peek_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			error_print("End of file while parsing math block starting at:\n");
			opens_at.print_error();
			mobius_error_exit();
		} else if((char)token.type == '}') {
			stream->read_token();
			if(block->exprs.size() == 0) {
				token.print_error_header();
				fatal_error("Empty math block.");
			}
			break;
		}
		
		token = stream->peek_token();
		Token token2 = stream->peek_token(1);
		if(token.type == Token_Type::identifier && token2.type == Token_Type::def) {
			auto local_var = new Local_Var_AST();
			local_var->name = token;
			local_var->source_loc = token.source_loc;
			stream->read_token(); stream->read_token();
			auto expr = parse_math_expr(stream);
			stream->expect_token(',');
			local_var->exprs.push_back(expr);
			block->exprs.push_back(local_var);
		} else {
			auto expr = parse_potential_if_expr(stream);
			block->exprs.push_back(expr);
		}
	}
	
	return block;
}


Math_Expr_AST *
potentially_parse_regex_unary(Token_Stream *stream, Math_Expr_AST *arg) {
	Math_Expr_AST *result = arg;
	Token token = stream->peek_token();
	if((char)token.type == '?' || (char)token.type == '*' || (char)token.type == '+') {
		auto unary = new Unary_Operator_AST();
		unary->oper = token.type;
		unary->exprs.push_back(arg);
		result = unary;
		stream->read_token();
	} else
		return result;
	return potentially_parse_regex_unary(stream, result);
}

Math_Expr_AST *
parse_primary_regex(Token_Stream *stream) {
	
	Math_Expr_AST *result = nullptr;
	Token token = stream->peek_token();
	if(token.type == Token_Type::identifier) {
		auto ident = new Regex_Identifier_AST();
		ident->ident = token;
		ident->source_loc = token.source_loc;
		result = ident;
		stream->read_token();
	} else if((char)token.type == '(') {
		stream->read_token();
		result = parse_regex_list(stream, token.source_loc, false);
		//stream->expect_token(')'); // no, this is already taken care of by parse_regex_list
	} else {
		token.print_error_header();
		fatal_error("Unexpected token '", token.string_value, "' while parsing regex.");
	}
	result = potentially_parse_regex_unary(stream, result);
	
	return result;
}

Math_Expr_AST *
parse_regex_expr(Token_Stream *stream) {
	std::vector<Math_Expr_AST *> exprs;
	exprs.push_back(parse_primary_regex(stream));
	while(true) {
		Token token = stream->peek_token();
		if((char)token.type == '|') {
			stream->read_token(); // consume the |
			exprs.push_back(parse_primary_regex(stream));
		} else
			break;
	}
	Math_Expr_AST *result = nullptr;
	if(exprs.size() == 1)
		result = exprs[0];
	else {
		result = new Regex_Or_Chain_AST();
		result->exprs = exprs;
		result->source_loc = exprs[0]->source_loc;
	}
	return result;
}

Math_Expr_AST *
parse_regex_list(Token_Stream *stream, Source_Location opens_at, bool outer) {
	std::vector<Math_Expr_AST *> exprs;
	
	while(true) {
		Token token = stream->peek_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			error_print("End of file while parsing regex block starting at:\n");
			opens_at.print_error();
			mobius_error_exit();
		} else if((char)token.type == '}' || (char)token.type == ')') {
			stream->read_token();
			if(outer && (char)token.type == ')' || !outer && (char)token.type == '}') {
				token.print_error_header();
				fatal_error("Mismatching paranthesis type.");
			}
			if(exprs.size() == 0) {
				token.print_error_header();
				fatal_error("Empty regex block.");
			}
			break;
		}
		
		auto expr = parse_regex_expr(stream);
		exprs.push_back(expr);
	}
	
	Math_Expr_AST *result = nullptr;
	if(exprs.size() == 1)
		result = exprs[0];
	else {
		result = new Math_Block_AST();
		result->exprs = exprs;
		result->source_loc = exprs[0]->source_loc;
	}
	
	return result;
}

int
match_declaration(Decl_AST *decl, const std::initializer_list<std::initializer_list<Arg_Pattern>> &patterns, 
	bool allow_handle, bool allow_body, bool allow_notes) {
	
	if(!allow_handle && decl->handle_name.string_value.count > 0) {
		decl->handle_name.print_error_header();
		fatal_error("A '", name(decl->type), "' declaration can not be assigned to an identifier.");
	}
	
	int found_match = -1;
	int idx = -1;
	for(const auto &pattern : patterns) {
		++idx;
		if(decl->args.size() < pattern.size() || (decl->args.size() > 0 && pattern.size() == 0)) continue;
		if(decl->args.size()==0 && pattern.size()==0) {
			found_match = idx;
			break;
		}
		
		bool cont = false;
		auto match = pattern.begin();
		int match_idx = 0;
		for(auto arg : decl->args) {
			if(!match->matches(arg)) {
				cont = true;
				break;
			}
			
			if(match->is_vararg) {
				if(match_idx != pattern.size()-1)
					fatal_error(Mobius_Error::internal, "Used a vararg that was not the last in the arg list when match_declaration().\n");
			} else {
				++match_idx;
				++match;
			}
			if(match == pattern.end() && match_idx != decl->args.size()) {
				cont = true;
				break;
			}
		}
		if(cont) continue;
		
		found_match = idx;
		break;
	}
	
	if(found_match == -1) {
		decl->source_loc.print_error_header();
		error_print("The arguments to the declaration of type '", name(decl->type), "' don't match any recognized pattern for this context. The recognized patterns are:\n");
		for(const auto &pattern : patterns) {
			if(pattern.size() == 0) {
				error_print("()\n");
				continue;
			}
			error_print("(");
			auto match = pattern.begin();
			while(true) {
				match->print_to_error();
				++match;
				if(match != pattern.end()) error_print(", ");
				else break;
			}
			error_print(")\n");
		}
		mobius_error_exit();
	}
	
	Body_Type body_type = get_body_type(decl->type);
	if((body_type == Body_Type::none || !allow_body) && !decl->bodies.empty()) {
		decl->bodies[0]->opens_at.print_error_header();
		fatal_error("This '", name(decl->type), "' declaration should not have a body.");
	}
	
	if(allow_notes) {
		bool found_main = false;
		std::unordered_set<String_View, String_View_Hash> found_notes;
		
		for(auto body : decl->bodies) {
			if(is_valid(&body->note)) {
				if(found_notes.find(body->note.string_value) != found_notes.end()) {
					body->note.print_error_header();
					fatal_error("Duplicate note '", body->note.string_value, "' for this declaration.");
				}
				found_notes.insert(body->note.string_value);
			} else {
				if(found_main) {
					body->opens_at.print_error_header();
					fatal_error("Duplicate main (note-free) body for this declaration.");
				}
				found_main = true;
			}
		}
	} else if(decl->bodies.size() > 1) {
		decl->bodies[1]->opens_at.print_error_header();
		fatal_error("This declaration should not have more than one body.");
	}
	
	return found_match;
}

bool
Arg_Pattern::matches(Argument_AST *arg) const {
	Token_Type check_type = token_type;
	
	switch(pattern_type) {
		
		case Type::any : {
			return true;
		} break;
		
		case Type::decl : {
			if(arg->decl && (get_reg_type(arg->decl->type) == get_reg_type(decl_type))) return true;
			//NOTE: we could still have an identifier that could potentially resolve to this type
			check_type = Token_Type::identifier;
		} // fall through to the next case to see if we have an identifier.
		
		case Type::value : {
			if(arg->chain.size() == 1) {
				if(check_type == Token_Type::real)
					return is_numeric(arg->chain[0].type);
				return arg->chain[0].type == check_type;
				
			} else if(arg->chain.size() > 1 && check_type == Token_Type::identifier) {
				if(pattern_type == Type::decl)
					return false; // Only a single token can refer to a decl.
				for(Token &token : arg->chain) {  //TODO: Not sure if we could ever get a chain of non-identifiers from the ast generation any way? So this check may be superfluous.
					if(token.type != Token_Type::identifier)
						return false;
				}
				return true;
			}
		}
	}
	return false;
}

void
check_allowed_serial_name(String_View serial_name, Source_Location &loc) {
	for(int idx = 0; idx < serial_name.count; ++idx) {
		char c = serial_name[idx];
		if(c == ':' || c == '.') {
			loc.print_error_header();
			fatal_error("The symbol '", c, "' is not allowed inside a name.");
		}
	}
}

bool
is_reserved(const std::string &handle) {
	static std::string reserved[] = {
		#define ENUM_VALUE(name, _a, _b) #name,
		#include "decl_types.incl"
		#undef ENUM_VALUE
		
		#define ENUM_VALUE(name) #name,
		#include "special_directives.incl"
		#undef ENUM_VALUE
		
		#include "other_reserved.incl"
	};
	return (std::find(std::begin(reserved), std::end(reserved), handle) != std::end(reserved));
}

