
#include "../src/linear_memory.h"
#include "../src/peek_queue.h"
#include "../src/file_utils.h"
#include "../src/datetime.h"
#include "../src/lexer.h"
#include "../src/ast.h"
#include "../src/model_declaration.h"
#include "../src/emulate.h"
#include "../src/model_application.h"

//#include <windows.h>   // If you want to use SetConsoleOutputCP

/*
void
write_result_data(String_View file_name, Model_Application *app) {
	FILE *file = open_file(file_name, "w");
	
	std::vector<s64> offsets;
	for(auto var_id : app->vars.all_state_vars()) {
		String_View name = app->vars[var_id]->name;
		//if(name != "Quick flow" && name != "in_flux") continue;
		app->result_structure.for_each(var_id, [&](std::vector<Index_T> &indexes, s64 offset) {
			fprintf(file, "\"%.*s\"[", name.count, name.data);
			for(auto index : indexes) fprintf(file, "%d ", index.index);
			fprintf(file, "]\t");
			offsets.push_back(offset);
		});
	}
	fprintf(file, "\n");

	for(s64 ts = -1; ts < app->data.results.time_steps; ++ts) {
		for(s64 offset : offsets) {
			fprintf(file, "%f\t", *(app->data.results.get_value(offset, ts)));
		}
		fprintf(file, "\n");
	}
	
	fclose(file);
}
*/

int
main(int argc, char** argv) {
	//SetConsoleOutputCP(65001);
	
	String_View model_file = "models/stupidly_simple_model.txt";
	String_View input_file = "models/stupidly_simple_data.dat";
	
	if(argc >= 2)
		model_file = argv[1];
	if(argc >= 3)
		input_file = argv[2];
	
	auto config = load_config("my_config.txt");
	Mobius_Model *model = load_model(model_file, &config);
	
	std::cout << "Loading done.\n";
	
	{
		Model_Application app(model);
		
		Data_Set data_set;
		data_set.read_from_file(input_file);
		
		app.build_from_data_set(&data_set);

		app.compile();
		
		run_model(&app);
		
		//write_result_data("results.dat", &app);
		
		app.save_to_data_set();
		data_set.write_to_file("models/test_save_data.dat");
	}
	
	delete model;
		
	std::cout << "Finished correctly.\n";
}