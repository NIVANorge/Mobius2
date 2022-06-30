
#include "ast.h"


bool
is_accepted_for_chain(Token_Type type, bool identifier_only) {
	return (identifier_only && type == Token_Type::identifier) || (!identifier_only && can_be_value_token(type));
}

void
read_identifier_chain(Token_Stream *stream, char separator, std::vector<Token> *list_out, bool identifier_only = true) {
	while(true) {
		Token token = stream->read_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			fatal_error("End of file while parsing identifier chain.");  //TODO: give location of where it started.
		}
		if(!is_accepted_for_chain(token.type, identifier_only)) {
			token.print_error_header();
			fatal_error("Misformatted chain: \"", token.string_value, "\".");
		}
		list_out->push_back(token);
		token = stream->peek_token();
		if(separator == ' ') {
			if(!is_accepted_for_chain(token.type, identifier_only)) break;
		} else {
			if((char)token.type == separator)
				stream->read_token();
			else
				break;
		}
	}
}

Decl_Type
decl_type(Token *string_name, Body_Type *body_type_out) {
	#define ENUM_VALUE(name, body_type, _) if(string_name->string_value == #name) { *body_type_out = Body_Type::body_type; return Decl_Type::name; }
	#include "decl_types.incl"
	#undef ENUM_VALUE
	
	string_name->print_error_header();
	fatal_error("Unrecognized declaration type \"", string_name->string_value, "\".");
	
	return Decl_Type::unrecognized;
}

void print_expr(Math_Expr_AST *expr) {
	warning_print("(");
	if(expr->type == Math_Expr_Type::binary_operator) {
		auto binop = reinterpret_cast<Binary_Operator_AST *>(expr);
		print_expr(binop->exprs[0]);
		warning_print(binop->oper);
		print_expr(binop->exprs[1]);
	} else if(expr->type == Math_Expr_Type::literal) {
		auto literal = reinterpret_cast<Literal_AST *>(expr);
		warning_print(literal->value.double_value());
	} else {
		warning_print("something");
	}
	warning_print(")");
}


Decl_AST *
parse_decl(Token_Stream *stream, Linear_Allocator *allocator) {
	
	Decl_AST *decl = allocator->make_new<Decl_AST>();
	
	Token ident = stream->expect_token(Token_Type::identifier);
	Token next  = stream->read_token();

	if((char)next.type == ':') {
		decl->handle_name = ident;
		ident = stream->expect_token(Token_Type::identifier);
		next  = stream->read_token();
	}
	decl->decl_chain.push_back(ident);
	
	if((char)next.type == '.') {
		read_identifier_chain(stream, '.', &decl->decl_chain);
		
		next = stream->read_token();
	}
	
	// We generally have something on the form a.b.type(bla) . The chain is now {a, b, type}, but we want to store the type separately from the rest of the chain.
	decl->type_name = decl->decl_chain.back();
	decl->decl_chain.pop_back();
	
	Body_Type body_type;
	decl->type = decl_type(&decl->type_name, &body_type);
	
	if((char)next.type != '(') {
		next.print_error_header();
		fatal_error("Expected a ( .");
	}
		
	while(true) {
		next = stream->peek_token();
		
		if((char)next.type == ')') {
			stream->read_token();
			break;
		}
		else if(can_be_value_token(next.type)) {
			Argument_AST *arg = allocator->make_new<Argument_AST>();
			
			Token peek = stream->peek_token(1);
			if(can_be_value_token(peek.type) || (char)peek.type == ')' || (char)peek.type == ',') {
				read_identifier_chain(stream, ' ', &arg->sub_chain, false);
				arg->chain_sep = ' ';
			}
			else if(next.type == Token_Type::identifier) {
				if((char)peek.type == '.') {
					read_identifier_chain(stream, '.', &arg->sub_chain);
					arg->chain_sep = '.';
				} else
					arg->decl = parse_decl(stream, allocator);
			} else {
				peek.print_error_header();
				fatal_error("Misformatted declaration argument.");
			}
			
			decl->args.push_back(arg);
			
			next = stream->peek_token();
			if((char)next.type == ',')
				stream->read_token();
			else if((char)next.type != ')') {
				next.print_error_header();
				fatal_error("Expected a ) or a ,");
			}
		}
		else {
			next.print_error_header();
			fatal_error("Misformatted declaration argument list."); //TODO: better error message.
		}
	}

	while(true) {
		next = stream->peek_token();
		char ch = (char)next.type;
		if(ch == '.' || ch == '{') {
			stream->read_token();
			Body_AST *body;
			
			if(body_type == Body_Type::decl) {
				body = allocator->make_new<Decl_Body_AST>();
			} else if(body_type == Body_Type::function) {
				body = allocator->make_new<Function_Body_AST>();
			} else if(body_type == Body_Type::none) {
				next.print_error_header();
				fatal_error("Declarations of type ", decl->type_name.string_value, " can't have declaration bodies.");
			}
			body->opens_at = next.location;
			
			if(ch == '.') {
				read_identifier_chain(stream, '.', &body->modifiers);
				next = stream->read_token();
			}
			
			if((char)next.type != '{') {
				next.print_error_header();
				fatal_error("Expected a {}-enclosed body for the declaration.");
			}
			
			if(body_type == Body_Type::decl) {
				auto decl_body = reinterpret_cast<Decl_Body_AST *>(body);
				while(true) {
					Token token = stream->peek_token();
					if(token.type == Token_Type::quoted_string) {
						stream->read_token();
						if(decl_body->doc_string.type == Token_Type::quoted_string) {
							// we already found one earlier.
							token.print_error_header();
							fatal_error("Multiple doc strings for declaration.");
						}
						decl_body->doc_string = token;
					} else if (token.type == Token_Type::identifier) {
						Decl_AST *child_decl = parse_decl(stream, allocator);
						decl_body->child_decls.push_back(child_decl);
					} else if ((char)token.type == '}') {
						stream->read_token();
						break;
					} else {
						token.print_error_header();
						fatal_error("Expected a doc string, a declaration, or a }");
					}
				}
			}
			else if(body_type == Body_Type::function) {
				auto function_body = reinterpret_cast<Function_Body_AST *>(body);
				function_body->block = parse_math_block(stream, allocator, next.location);
			}
			
			decl->bodies.push_back(body);
			
		} else
			break;
	}
	
	#if 0
	{
		warning_print("decl:\n");
		if(decl->handle_name.string_value.count)
			warning_print("handle: ", decl->handle_name.string_value, "\n");
		warning_print("chain: ");
		for (Token &token : decl->decl_chain)
			warning_print(token.string_value, " ");
		warning_print("\nargs: ");
		for (Argument_AST *arg : decl->args) {
			for(Token &token : arg->sub_chain)
				warning_print(token.string_value, arg->chain_sep);
			warning_print(",");
		}
		warning_print("\n");
		
		
		
		for(auto body : decl->bodies) {
			if(body->type == Body_Type::function) {
				auto *func = reinterpret_cast<Function_Body_AST *>(body);
				for(auto expr : func->block->exprs) {
					print_expr(expr);
					warning_print(";\n");
				}
			}
		}
		warning_print("\n\n");
	}
	#endif
	
	return decl;
}


Function_Call_AST *
parse_function_call(Token_Stream *stream, Linear_Allocator *allocator) {
	Function_Call_AST *function = allocator->make_new<Function_Call_AST>();
	
	function->name = stream->read_token();
	stream->read_token(); // consume the '('
	while(true) {
		Token token = stream->peek_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			error_print("End of file while parsing function argument list for function \"", function->name.string_value, "\" starting at:\n");
			function->name.print_error_location();
			mobius_error_exit();
		} else if((char)token.type == ')') {
			stream->read_token();
			break;
		}
		
		Math_Expr_AST *expr = parse_math_expr(stream, allocator);
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
find_binary_operator(Token_Stream *stream, String_View *result) {
	Token peek = stream->peek_token();
	char c = (char)peek.type;
	
	bool arit = (c == '+') || (c == '-') || (c == '*') || (c == '/');
	bool eq   = (c == '=') || (c == '!');
	bool comp = (c == '<') || (c == '>');
	bool logical = (c == '|') || (c == '&');
	
	if( arit || comp || eq || logical) {
		*result = peek.string_value;
		
		// Hmm, we could also have done this combining of two-char operators in the lexer ?? Would make it possible to disallow spaces inside    >   =     for instance. Also cleaner.
		// Actually,    >  =    would cause a bug now, since the = would not be counted in the operator string.
		if(eq || comp) {
			Token peek2 = stream->peek_token(1);
			if((char)peek2.type == '=') {
				++result->count;
			} else if (eq)    // we got a single = or ! sign, which is not a binary operator
				return 0;
		}
		
		if(*result == "|") return 1000;
		else if(*result == "&") return 2000;
		else if(*result == "<" || *result == ">" || *result == ">=" || *result == "<=" || *result == "==" || *result == "!=") return 3000; 
		else if(*result == "+" || *result == "-") return 4000;
		else if(*result == "/") return 5000;
		else if(*result == "*") return 6000;   //not sure if * should be higher than /
		else {
			peek.print_error_header();
			fatal_error("Unrecognized binary operator \"", *result, "\".");
		}
	}
	return 0;
}

Math_Expr_AST *
potentially_parse_binary_operation_rhs(Token_Stream *stream, Linear_Allocator *allocator, int prev_prec, Math_Expr_AST *lhs) {
	
	while(true) {
		String_View oper;
		if(int cur_prec = find_binary_operator(stream, &oper)) {	
			if(cur_prec < prev_prec)
				return lhs;
			
			for(int it = 0; it < oper.count; ++it)
				stream->read_token(); // consume the operator;
			
			Math_Expr_AST *rhs = parse_primary_expr(stream, allocator);
			
			String_View oper_next;
			if(int next_prec = find_binary_operator(stream, &oper_next)) {
				if(cur_prec < next_prec)
					rhs = potentially_parse_binary_operation_rhs(stream, allocator, cur_prec + 1, rhs);
			}
			
			Binary_Operator_AST *binop = allocator->make_new<Binary_Operator_AST>();
			binop->oper = oper;
			binop->exprs.push_back(lhs);
			binop->exprs.push_back(rhs);
			lhs = binop;
			
		} else
			return lhs;
	}
}

Math_Expr_AST *
parse_math_expr(Token_Stream *stream, Linear_Allocator *allocator) {
	auto lhs = parse_primary_expr(stream, allocator);
	return potentially_parse_binary_operation_rhs(stream, allocator, 0, lhs);
}
	
Math_Expr_AST *
parse_primary_expr(Token_Stream *stream, Linear_Allocator *allocator) {
	Math_Expr_AST  *result = nullptr;
	Token token = stream->peek_token();
	
	if((char)token.type == '-' || (char)token.type == '!') {
		stream->read_token();
		auto unary = allocator->make_new<Unary_Operator_AST>();
		unary->oper = token.string_value;
		unary->exprs.push_back(parse_primary_expr(stream, allocator));
		result = unary;
	} else if((char)token.type == '{') {
		stream->read_token();
		result = parse_math_block(stream, allocator, token.location);
	} else if (token.type == Token_Type::identifier) {
		Token peek = stream->peek_token(1);
		if((char)peek.type == '(') {
			result = parse_function_call(stream, allocator);
		} else {
			auto val = allocator->make_new<Identifier_Chain_AST>();
			read_identifier_chain(stream, '.', &val->chain, true);
			result = val;
		}
	} else if (is_numeric_or_bool(token.type)) {
		auto val = allocator->make_new<Literal_AST>();
		stream->read_token();
		val->value = token;
		result = val;
	} else if ((char)token.type == '(') {
		// todo fixup of precedence
		stream->read_token();
		result = parse_math_expr(stream, allocator);
		Token token = stream->read_token();
		if((char)token.type != ')') {
			token.print_error_header();
			fatal_error("Expected a ')' .");
		}
	} else {
		token.print_error_header();
		fatal_error("Unexpected token ", token.string_value);
	}
	
	return result;
}

Math_Expr_AST *
parse_potential_if_expr(Token_Stream *stream, Linear_Allocator *allocator) {
	Math_Expr_AST *value = parse_math_expr(stream, allocator);
	Token token = stream->peek_token();
	if(token.type == Token_Type::identifier && token.string_value == "if") {
		stream->read_token(); // consume the if
		
		Math_Expr_AST *condition = parse_math_expr(stream, allocator);
		
		If_Expr_AST *if_expr = allocator->make_new<If_Expr_AST>();
		if_expr->exprs.push_back(value);
		if_expr->exprs.push_back(condition);
		
		while(true) {
			stream->expect_token(',');
			
			value = parse_math_expr(stream, allocator);
			token = stream->read_token();
			
			if(token.type == Token_Type::identifier) {
				if(token.string_value == "if") {
					condition = parse_math_expr(stream, allocator);
					if_expr->exprs.push_back(value);
					if_expr->exprs.push_back(condition);
					continue;
				} else if (token.string_value == "otherwise") {
					if_expr->exprs.push_back(value);
					break;
				}
			}
			
			token.print_error_header();
			fatal_error("Expected an \"if\" or an \"otherwise\".");
		}
		
		return if_expr;
	}
	return value;
}

Math_Block_AST *
parse_math_block(Token_Stream *stream, Linear_Allocator *allocator, Source_Location opens_at) {
	Math_Block_AST *block = allocator->make_new<Math_Block_AST>();
	block->opens_at = opens_at;
	
	int semicolons = 0;
	while(true) {
		Token token = stream->peek_token();
		if(token.type == Token_Type::eof) {
			token.print_error_header();
			error_print("End of file while parsing math block starting at:\n");
			opens_at.print_error();
			mobius_error_exit();
		}
		else if((char)token.type == '}') {
			stream->read_token();
			if(block->exprs.size() == 0) {
				token.print_error_header();
				fatal_error("Empty math block.");
			} else if (semicolons >= block->exprs.size()) {
				token.print_error_header();
				fatal_error("The final statement in a block should not be terminated with a ; .");
			}
			break;
		}
		
		//TODO: assignments.. like   a := 5;
		
		auto expr = parse_potential_if_expr(stream, allocator);
		block->exprs.push_back(expr);
		
		token = stream->peek_token();
		if((char)token.type == ';') {
			++semicolons;
			stream->read_token();
		} else if((char)token.type != '}') {
			token.print_error_header();
			fatal_error("Expected ; or } , got \"", token.string_value, "\".");
		}
	}
	
	return block;
}

