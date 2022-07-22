
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
	
	std::vector<s64> offsets;
	for(auto var_id : model->state_vars) {
		String_View name = model->state_vars[var_id]->name;
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

#define USE_SIMPLE 1

int main() {
	//SetConsoleOutputCP(65001);
	
#if USE_SIMPLE
	//Mobius_Model *model = load_model("stupidly_simple_model.txt");
	Mobius_Model *model = load_model("lv_model.txt");
#else
	Mobius_Model *model = load_model("test_model.txt");
#endif
	model->compose();
	
	std::cout << "Composition done.\n";
	
	s64 time_steps = 100;
	
	Model_Application app(model);
	
	std::vector<String_View> indexes = {"a", "b", "c", "d"};
	for(auto index_set : model->modules[0]->index_sets)
		app.set_indexes(index_set, Array<String_View>{indexes.data(), indexes.size()});
	
	app.set_up_parameter_structure();
	app.set_up_series_structure();

#if !USE_SIMPLE
	//haaaack!
	Entity_Id par_id = model->modules[2]->find_handle("fc");
	std::vector<Index_T> par_idx = {Index_T{model->modules[0]->find_handle("lu"), 0}};
	auto offset = app.parameter_data.get_offset_alternate(par_id, &par_idx);
	warning_print("par offset is ", offset, "\n");
	app.parameter_data.data[offset].val_real = 50.0;
#else
	/*
	Entity_Id par_id = model->modules[1]->find_handle("par");
	std::vector<Index_T> par_idx = {Index_T{model->modules[0]->find_handle("idx"), 1}};
	auto offset = app.parameter_data.get_offset_alternate(par_id, &par_idx);
	warning_print("par offset is ", offset, "\n");
	app.parameter_data.data[offset].val_real = 50.0;
	*/
#endif
	app.compile();

#if !USE_SIMPLE
	app.series_data.allocate(time_steps);
	read_input_data("testinput.dat", &app);
#endif
	
	run_model(&app, time_steps);
	
	write_result_data("results.dat", &app);
		
	std::cout << "Finished correctly.\n";
}