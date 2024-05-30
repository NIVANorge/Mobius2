include("mobius.jl")
using .mobius
using Plots


app = setup_model("../models/simplyq_model.txt", "../models/data/simplyq_simple.dat")

run_model(app)

#TODO: Pack into convenience function
river = get_entity(app, invalid_entity_id, "river")
water = get_entity(app, invalid_entity_id, "water")
flow  = get_entity(app, invalid_entity_id, "flow")

flow_var = get_var_id_from_list(app, [river, water, flow])
steps = get_steps(app, flow_var)

flow_results = get_series_data(app, flow_var, ["Coull"])

x = 1:length(flow_results)
plot(x, flow_results)
savefig("flow_plot.png")