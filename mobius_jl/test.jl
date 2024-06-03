include("mobius.jl")
using .mobius
using Plots


app = setup_model("../models/simplyq_model.txt", "../models/data/simplyq_simple.dat", true)

run_model(app)

flow_var = get_var(app, ["river", "water", "flow"])
# Alternatively
# flow_var = get_var_by_name(app, "Reach flow")

flow_results = flow_var["Coull"]
dates = get_dates(flow_var)

plot(dates, flow_results)
savefig("flow_plot.png")