
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

void write_result_data(String_View file_name, Model_Application *model_app) {
	FILE *file = open_file(file_name, "w");
	
	auto model = model_app->model;
	
	for(auto &array : model_app->result_data.structure) {
		for(auto id : array.handles) {
			String_View name = model->state_vars[id]->name;
			fprintf(file, "\"%.*s\"\t", name.count, name.data);
		}
	}
	fprintf(file, "\n");
	
	std::vector<Index_T> indexes = { Index_T { Entity_Id { } , 0 } }; //TODO!
	
	for(s64 ts = -1; ts < model_app->result_data.time_steps; ++ts) {
		for(auto &array : model_app->result_data.structure) {
			for(auto id : array.handles) {
				auto offset = model_app->result_data.get_offset(id, &indexes);
				fprintf(file, "%f\t", *(model_app->result_data.get_value(offset, ts)));
			}
		}
		fprintf(file, "\n");
	}
	
	fclose(file);
}

#define USE_SIMPLE 0

int main() {
	//SetConsoleOutputCP(65001);
	
#if USE_SIMPLE
	Mobius_Model *model = load_model("stupidly_simple_model.txt");
#else
	Mobius_Model *model = load_model("test_model.txt");
#endif
	model->compose();
	
	std::cout << "Composition done.\n";
	
	s64 time_steps = 100;
	
	Model_Application app(model);
	app.set_up_parameter_structure();
	app.set_up_series_structure();
	
	app.compile();

#if !USE_SIMPLE
	app.series_data.allocate(time_steps);
	read_input_data("testinput.dat", &app);
#endif
	
	emulate_model_run(&app, time_steps);
	
	write_result_data("results.dat", &app);
		
	std::cout << "Finished correctly.\n";
}