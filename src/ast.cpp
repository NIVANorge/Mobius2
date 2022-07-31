
#include "ast.h"


Argument_AST::~Argument_AST() { delete decl; }
Function_Body_AST::~Function_Body_AST() { delete block; }


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
get_decl_type(Token *string_name, Body_Type *body_type_out) {
	#define ENUM_VALUE(name, body_type, _) if(string_name->string_value == #name) { *body_type_out = Body_Type::body_type; return Decl_Type::name; }
	#include "decl_types.incl"
	#undef ENUM_VALUE
	
	string_name->print_error_header();
	fatal_error("Unrecognized declaration type \"", string_name->string_value, "\".");
	
	return Decl_Type::unrecognized;
}

Body_Type
get_body_type(Decl_Type decl_type) {
	#define ENUM_VALUE(name, body_type, _) if(decl_type == Decl_Type::name) { return Body_Type::body_type; }
	#include "decl_types.incl"
	#undef ENUM_VALUE
	return Body_Type::none;
}

void print_expr(Math_Expr_AST *expr) {
	warning_print("(");
	if(expr->type == Math_Expr_Type::binary_operator) {
		auto binop = reinterpret_cast<Binary_Operator_AST *>(expr);
		print_expr(binop->exprs[0]);
		warning_print(name(binop->oper));
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
parse_decl(Token_Stream *stream) {
	
	Decl_AST *decl = new Decl_AST();
	
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
	decl->location = decl->decl_chain.back().location;
	
	Body_Type body_type;
	decl->type = get_decl_type(&decl->decl_chain.back(), &body_type);
	decl->decl_chain.pop_back(); // note: we only want to keep the first symbols (denoting location) in the chain since we now have stored the type separately.
	
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
			Argument_AST *arg = new Argument_AST();
			
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
					arg->decl = parse_decl(stream);
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
				body = new Decl_Body_AST();
			} else if(body_type == Body_Type::function) {
				body = new Function_Body_AST();
			} else if(body_type == Body_Type::none) {
				next.print_error_header();
				fatal_error("Declarations of type ", name(decl->type), " can't have declaration bodies.");
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
			}
			else if(body_type == Body_Type::function) {
				auto function_body = reinterpret_cast<Function_Body_AST *>(body);
				function_body->block = parse_math_block(stream, next.location);
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
parse_function_call(Token_Stream *stream) {
	Function_Call_AST *function = new Function_Call_AST();
	
	function->name = stream->read_token();
	function->location = function->name.location;
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
find_binary_operator(Token_Stream *stream, Token_Type *t) {
	Token peek = stream->peek_token();
	*t = peek.type;
	char c = (char)*t;
	
	if(c == '|') return 1000;
	else if(c == '&') return 2000;
	else if((c == '<') || (c == '>') || (*t == Token_Type::leq) || (*t == Token_Type::geq) || (c == '=') || (*t == Token_Type::neq)) return 3000; 
	else if((c == '+') || (c == '-')) return 4000;
	else if(c == '/') return 5000;
	else if(c == '*' || c == '%') return 6000;   //not sure if * should be higher than /
	else if(c == '^') return 7000;
	
	return 0;
}

Math_Expr_AST *
potentially_parse_binary_operation_rhs(Token_Stream *stream, int prev_prec, Math_Expr_AST *lhs) {
	
	while(true) {
		Token_Type oper;
		if(int cur_prec = find_binary_operator(stream, &oper)) {	
			if(cur_prec < prev_prec)
				return lhs;
			
			Token token = stream->read_token(); // consume the operator
			
			Math_Expr_AST *rhs = parse_primary_expr(stream);
			
			Token_Type oper_next;
			if(int next_prec = find_binary_operator(stream, &oper_next)) {
				if(cur_prec < next_prec)
					rhs = potentially_parse_binary_operation_rhs(stream, cur_prec + 1, rhs);
			}
			
			Binary_Operator_AST *binop = new Binary_Operator_AST();
			binop->oper = oper;
			binop->exprs.push_back(lhs);
			binop->exprs.push_back(rhs);
			binop->location = token.location;
			lhs = binop;
			
		} else
			return lhs;
	}
}

Math_Expr_AST *
parse_math_expr(Token_Stream *stream) {
	auto lhs = parse_primary_expr(stream);
	return potentially_parse_binary_operation_rhs(stream, 0, lhs);
}
	
Math_Expr_AST *
parse_primary_expr(Token_Stream *stream) {
	Math_Expr_AST  *result = nullptr;
	Token token = stream->peek_token();
	
	if((char)token.type == '-' || (char)token.type == '!') {
		Source_Location location = token.location;
		stream->read_token();
		auto unary = new Unary_Operator_AST();
		unary->oper = token.type;
		unary->exprs.push_back(parse_primary_expr(stream));
		unary->location = location;
		result = unary;
	} else if((char)token.type == '{') {
		stream->read_token();
		result = parse_math_block(stream, token.location);
	} else if (token.type == Token_Type::identifier) {
		Token peek = stream->peek_token(1);
		if((char)peek.type == '(') {
			result = parse_function_call(stream);
		} else {
			auto val = new Identifier_Chain_AST();
			val->location = token.location;
			read_identifier_chain(stream, '.', &val->chain, true);
			result = val;
		}
	} else if (is_numeric_or_bool(token.type)) {
		auto val = new Literal_AST();
		val->location = token.location;
		stream->read_token();
		val->value = token;
		result = val;
	} else if ((char)token.type == '(') {
		// todo fixup of precedence
		stream->read_token();
		result = parse_math_expr(stream);
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
parse_potential_if_expr(Token_Stream *stream) {
	Math_Expr_AST *value = parse_math_expr(stream);
	Token token = stream->peek_token();
	if(token.type == Token_Type::identifier && token.string_value == "if") {
		Source_Location location = token.location;
		stream->read_token(); // consume the if
		
		Math_Expr_AST *condition = parse_math_expr(stream);
		
		auto if_expr = new If_Expr_AST();
		if_expr->location = location;
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
			fatal_error("Expected an \"if\" or an \"otherwise\".");
		}
		
		return if_expr;
	}
	return value;
}

Math_Block_AST *
parse_math_block(Token_Stream *stream, Source_Location opens_at) {
	auto block = new Math_Block_AST();
	block->location = opens_at;
	
	//int semicolons = 0;
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
			} /*else if (semicolons >= block->exprs.size()) {
				token.print_error_header();
				fatal_error("The final statement in a block should not be terminated with a ; .");
			}*/
			break;
		}
		
		//TODO: assignments.. like   a := 5;
		token = stream->peek_token();
		Token token2 = stream->peek_token(1);
		if(token.type == Token_Type::identifier && token2.type == Token_Type::def) {
			auto local_var = new Local_Var_AST();
			local_var->name = token;
			local_var->location = token.location;
			stream->read_token(); stream->read_token();
			auto expr = parse_math_expr(stream);
			local_var->exprs.push_back(expr);
			block->exprs.push_back(local_var);
		} else {
			auto expr = parse_potential_if_expr(stream);
			block->exprs.push_back(expr);
		}
		
		//token = stream->peek_token();
		/*if((char)token.type == ';') {
			++semicolons;
			stream->read_token();
		} else if((char)token.type != '}') {
			token.print_error_header();
			fatal_error("Expected ; or } , got \"", token.string_value, "\".");
		}
		*/
	}
	
	return block;
}

int
match_declaration(Decl_AST *decl, const std::initializer_list<std::initializer_list<Arg_Pattern>> &patterns, int allow_chain, bool allow_handle, int allow_body_count, bool allow_body_modifiers) {
	// allow_chain = 0 means no chain. allow_chain=-1 means any length. allow_chain = n means only of length n exactly.
	
	//TODO: need much better error messages!
	
	if(!allow_chain && !decl->decl_chain.empty()) {
		decl->decl_chain[0].print_error_header();
		fatal_error("This should not be a chained declaration.");
	}
	if(allow_chain > 0 && decl->decl_chain.size() != allow_chain) {
		decl->decl_chain[0].print_error_header();
		fatal_error("There should be ", allow_chain, " elements in the declaration chain. We found ", decl->decl_chain.size(), ".");
	}
	if(!allow_handle && decl->handle_name.string_value.count > 0) {
		decl->handle_name.print_error_header();
		fatal_error("This declaration should not have a handle");
	}
	
	int found_match = -1;
	int idx = -1;
	for(const auto &pattern : patterns) {
		++idx;
		if(decl->args.size() != pattern.size()) continue;
		
		bool cont = false;
		auto match = pattern.begin();
		for(auto arg : decl->args) {
			if(!match->matches(arg)) {
				cont = true;
				break;
			}
			++match;
		}
		if(cont) continue;
		
		found_match = idx;
		break;
	}
	
	if(found_match == -1 && patterns.size() > 0) {
		decl->location.print_error_header();
		error_print("The arguments to the declaration \"", name(decl->type), "\" don't match any recognized pattern. The recognized patterns are:\n");
		for(const auto &pattern : patterns) {
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
	
	// NOTE: This check is only relevant if this type of declaration is allowed to have bodies at all. If a declaration that should not have a body gets one, that will be caught at the AST parsing stage.
	Body_Type body_type = get_body_type(decl->type);
	if(body_type != Body_Type::none && allow_body_count >= 0 && allow_body_count != decl->bodies.size()) {
		decl->location.print_error_header();
		fatal_error("Expected ", allow_body_count, " bodies for this declaration, got ", decl->bodies.size(), ".");
	}
	
	if(!allow_body_modifiers) {
		for(auto body : decl->bodies) {
			if(body->modifiers.size() > 0) {
				decl->location.print_error_header();
				fatal_error("The bodies of this declaration should not have modifiers.");
			}
		}
	}
	
	return found_match;
}