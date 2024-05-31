include("mobius.jl")
using .mobius
using Plots


app = setup_model("../models/simplyq_model.txt", "../models/data/simplyq_simple.dat", true)

run_model(app)

flow_var = get_var_id(app, ["river", "water", "flow"])
#Alternatively
# flow_var = get_var_by_name(app, "Reach flow")

flow_results = get_series_data(app, flow_var, ["Coull"])

x = 1:length(flow_results)
plot(x, flow_results)
savefig("flow_plot.png")