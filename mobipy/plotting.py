
import numpy as np
import matplotlib.pyplot as plt
import corner

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

def plot_target(app, target, ax=None, sl=slice(None), legend=True) :
	if not ax :
		fig, ax = plt.subplots(1, figsize = (10, 2))
	
	simname, simidx, obsname, obsidx, *wgt = target
	app.var(obsname)[obsidx].loc[sl].plot(ax=ax, marker='o', linewidth=0, markerfacecolor=(0, 0, 0, 0), markeredgecolor='#c79124')
	app.var(simname)[simidx].loc[sl].plot(ax=ax, color='#3293e3')
	
	ymin, ymax = ax.get_ylim()
	if ymin > 0 :
		ax.set_ylim(0, ymax*1.1)
	
	if legend :
		ax.legend()

def plot_targets(app, targets, sl, width=10, height_per=5) :
	# For use with optimization targets.
	
	nplots = 1
	if isinstance(targets, list) :
		nplots = len(targets)
	
	fig, axs = plt.subplots(nplots, figsize=(width, height_per*nplots))
	
	if nplots > 1:
		for i, target in enumerate(targets) :
			plot_target(app, target, axs[i], sl)
	else :
		plot_target(app, targets, axs, sl)
		
	return fig, axs
	
def chain_plot(result, filename=None) :
	# For MCMC result

	chain = result.chain
	ndim = result.chain.shape[-1]
	labels = result.var_names

	samples = result.chain

	# Plot
	fig_height = max(30, len(labels)*3.5)

	fig, axes = plt.subplots(nrows=ndim, ncols=1, figsize=(10, fig_height))   
	for idx, label in enumerate(labels):        
		axes[idx].plot(samples[..., idx], '-', color='k', alpha=0.3)
		axes[idx].set_title(label, fontsize=12) 
	plt.subplots_adjust(hspace=0.5)   
	plt.tight_layout()
	if filename is not None :
		plt.savefig(filename)
	
def corner_plot(result, burn) :
	# For MCMC result
	
	# Remove burnin and flatten the chains.
	chain = result.chain[burn:, :, :]
	ndim = result.chain.shape[-1]
	samples = chain.reshape((-1, ndim))

	# Make a corner plot of the posterior distributions
	c = corner.corner(samples,
		labels=result.var_names,
		title_args={'fontsize':20},
		label_kwargs={'fontsize':18},
		verbose=False,
		quantiles=[0.025, 0.5, 0.975])