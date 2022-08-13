import sys
sys.path.append('../')
import mobipy

app = mobipy.Model_Application.build_from_model_and_data_file('models/simplyq_model.txt', 'models/simplyq_data.dat')

print("fc is %f" % app['SimplyQ land'].fc[["Arable"]])

app['SimplyQ land'].fc[["Arable"]] = 123

print("fc is now %f" % app['SimplyQ land'].fc[["Arable"]])