
import numpy as np
import matplotlib.pyplot as plt

def quick_heatmap(ax, data, dates, ys, cmap='coolwarm') :
	x, y = np.meshgrid(dates, ys)

	ax.pcolormesh(x, y, data.transpose(), cmap=cmap, shading='auto')
	ax.invert_yaxis()


def quick_surf(ax, data, dates, ys, cmap='coolwarm') :
	x, y = np.meshgrid(range(len(dates)), ys)  #TODO: Figure out how to put date axis on plot_surface

	ax.plot_surface(x, y, data.transpose(), cmap=cmap)
	ax.invert_xaxis()
	ax.view_init(50, 80)
	ax.set_box_aspect(aspect = (2,1,1))
	



def plot_targets(app, targets, sl, width=10, height_per=5) :
	# For use with optimization targets.
	
	def plot_target(app, target, ax) :
		simname, simidx, obsname, obsidx, *wgt = target
		app.var(simname)[simidx].loc[sl].plot(ax=ax)
		app.var(obsname)[obsidx].loc[sl].plot(ax=ax, marker='o')
		
		ymin, ymax = ax.get_ylim()
		if ymin > 0 :
			ax.set_ylim(0, ymax)
		
		ax.legend()
		
	
	fig, axs = plt.subplots(len(targets), figsize=(width, height_per*len(targets)))
	
	if isinstance(targets, list) :
		for i, target in enumerate(targets) :
			plot_target(app, target, axs[i])
	else :
		plot_target(app, targets, axs)
		
	return fig, axs