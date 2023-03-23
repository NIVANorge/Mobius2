

#ifndef MOBIUS_EFFECT_INDEXES_H
#define MOBIUS_EFFECT_INDEXES_H

#include <vector>

void compute_effect_indexes(int n_samples, int n_pars, int n_workers, int sample_method, double *min_bound, double *max_bound, double (*target_fun)(void *, int, const std::vector<double> &), void *target_state, bool (*callback)(void *, int, double, double), void *callback_state, int callback_interval, std::vector<double> &samples_out);


#endif // MOBIUS_EFFECT_INDEXES_H