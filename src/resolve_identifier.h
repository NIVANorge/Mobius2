
#ifndef MOBIUS_RESOLVE_IDENTIFIER_H
#define MOBIUS_RESOLVE_IDENTIFIER_H

#include "model_declaration.h"

struct
Location_Resolve {
	Variable_Type       type;
	Entity_Id           val_id = invalid_entity_id;
	Var_Location        loc;
	Var_Loc_Restriction restriction;
};

void
resolve_simple_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Var_Location &loc);

void
resolve_flux_loc_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Specific_Var_Location &loc);

void
resolve_loc_decl_argument(Mobius_Model *model, Decl_Scope *scope, Argument_AST *arg, Loc_Registration &loc_decl);

void
resolve_full_location(Mobius_Model *model, Location_Resolve &result, const std::vector<Token> &chain, const std::vector<Token> &bracket, const std::vector<Token> &bracket2, Decl_Scope *scope, bool *error, Entity_Id implicit_conn_id = invalid_entity_id);

#endif // MOBIUS_RESOLVE_IDENTIFIER_H