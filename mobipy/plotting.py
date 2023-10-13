
import numpy as np

def quick_heatmap(ax, data, dates, ys) :
	x, y = np.meshgrid(dates, ys)

	ax.pcolormesh(x, y, data.transpose(), cmap='coolwarm', shading='auto')
	ax.invert_yaxis()


def quick_surf(ax, data, dates, ys) :
	x, y = np.meshgrid(range(len(dates)), ys)  #TODO: Figure out how to put date axis on plot_surface

	ax.plot_surface(x, y, data.transpose(), cmap='coolwarm')
	ax.invert_xaxis()
	ax.view_init(50, 80)
	ax.set_box_aspect(aspect = (2,1,1))