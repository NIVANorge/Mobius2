
#include "../src/linear_memory.h"
#include "../src/peek_queue.h"
#include "../src/file_utils.h"
#include "../src/datetime.h"
#include "../src/lexer.h"
#include "../src/ast.h"
#include "../src/model_declaration.h"
#include "../src/emulate.h"
#include "../src/model_application.h"

//#include <windows.h>

/*
void read_input_data(String_View file_name, Model_Application *model_app) {	
	// Dummy code for initial prototype. We should reuse stuff from Mobius 1.0 later!
	
	String_View file_data = read_entire_file(file_name);
	Token_Stream stream(file_name, file_data);
	
	std::vector<std::vector<Var_Id>> order;
	
	Mobius_Model *model = model_app->model;
	
	while(true) {
		auto token = stream.peek_token();
		if(token.type != Token_Type::quoted_string) break;
		stream.read_token();
		order.resize(order.size() + 1);
		String_View name = token.string_value;
		for(auto id : *model->series[name]) order[order.size()-1].push_back(id);
	}
	
	std::vector<Index_T> indexes;
	
	size_t count = order.size();
	for(s64 ts = 0; ts < model_app->series_data.time_steps; ++ts)
		for(int idx = 0; idx < count; ++idx) {
			double val = stream.expect_real();
			for(auto id : order[idx]) {
				auto offset = model_app->series_data.get_offset(id, &indexes);
				*(model_app->series_data.get_value(offset, ts)) = val;
			}
		}
	free(file_data.data);
}
*/

void write_result_data(String_View file_name, Model_Application *model_app) {
	FILE *file = open_file(file_name, "w");
	
	auto model = model_app->model;
	
	std::vector<s64> offsets;
	for(auto var_id : model->state_vars) {
		String_View name = model->state_vars[var_id]->name;
		//if(name != "Quick flow" && name != "in_flux") continue;
		model_app->result_data.for_each(var_id, [&](std::vector<Index_T> *indexes, s64 offset) {
			fprintf(file, "\"%.*s\"[", name.count, name.data);
			for(auto index : *indexes) fprintf(file, "%d ", index.index);
			fprintf(file, "]\t");
			offsets.push_back(offset);
		});
	}
	fprintf(file, "\n");

	for(s64 ts = -1; ts < model_app->result_data.time_steps; ++ts) {
		for(s64 offset : offsets) {
			fprintf(file, "%f\t", *(model_app->result_data.get_value(offset, ts)));
		}
		fprintf(file, "\n");
	}
	
	fclose(file);
}


int main(int argc, char** argv) {
	//SetConsoleOutputCP(65001);
	
	String_View model_file = "stupidly_simple_model.txt";
	String_View input_file = "";
	int nidx1 = 1;
	int nidx2 = 1;
	
	if(argc >= 2)
		model_file = argv[1];
	if(argc >= 3)
		input_file = argv[2];
	
	Mobius_Model *model = load_model(model_file);
	model->compose();
	
	std::cout << "Composition done.\n";
	
	{
		Model_Application app(model);
		
		Data_Set data_set;
		data_set.read_from_file(input_file);
		
		app.build_from_data_set(&data_set);

		app.compile();
		
		run_model(&app);
		
		write_result_data("results.dat", &app);
		
	}
	
	delete model;
		
	std::cout << "Finished correctly.\n";
}