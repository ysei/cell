#include "cell.h"
#include <cmath>
#include <thrust/sort.h>
#include <thrust/fill.h>
#include <thrust/scan.h>
#include <thrust/iterator/retag.h>
#include <thrust/system/omp/memory.h>
#include <algorithm>
#include <iostream>
#include "parallel_radix_sort.h"

Cell::Cell(fp width, fp height, fp dx) : dx(dx) {
    w = std::ceil(width/dx);
    h = std::ceil(height/dx);

    grid = new int[w * h];
    thrust::fill(&grid[0], &grid[w * h - 1], 0);
}

Cell::~Cell() {
    delete[] grid;
 }

int Cell::idx(const vector &v) const {
    assert(v.x >= 0 && v.x <= w*dx);
    assert(v.y >= 0 && v.y <= h*dx);
    int _idx = int(v.y/dx)*w + int(v.x/dx);
    assert(_idx < w * h);
    assert(_idx >= 0);
    return _idx;
}

void Cell::add_particles(std::vector<vector> &particles) {
    if (cell_indices.size() != particles.size()) cell_indices.resize(particles.size());

#pragma omp parallel for
    for (std::size_t i = 0; i < particles.size(); ++i) {
        cell_indices[i] = idx(particles[i]);
    }

#if 0
    thrust::sort_by_key(thrust::retag<thrust::omp::tag>(cell_indices.begin()),
                        thrust::retag<thrust::omp::tag>(cell_indices.end()),
                        thrust::retag<thrust::omp::tag>(particles.begin()));
#else
    int *ci_ptr = &cell_indices[0];
    vector *p_ptr = &particles[0];
    parallel_radix_sort::SortPairs(ci_ptr, p_ptr, particles.size());
#endif

    grid[cell_indices[0]] = 0;

#pragma omp parallel for
    for (int i = 1; i < cell_indices.size(); ++i) {
        if (cell_indices[i-1] != cell_indices[i]) {
            grid[cell_indices[i]] = i;
        }
    }
#if 0
    int start = 0;

    for (int i = 1; i < w*h; ++i) {
        int stop = grid[i];
        int _idx = cell_indices[start];
        assert(cell_indices[stop] != cell_indices[stop - 1]);

        for (int j = start; j < stop; ++j) {
            assert(_idx == cell_indices[j]);
        }
        start = stop;
    }
#endif

    this->particles = &particles;
}

void Cell::cell(int _idx, int *start, int *stop) const {
    assert(_idx < w*h);
    *start = grid[_idx];
    *stop = _idx == (w*h-1)
            ? particles->size()
            : grid[_idx+1];
    assert(*stop >= 0 && *stop <= particles->size());
    assert(*start >= 0 && *start <= particles->size());
    //can this be always satified even on empty cells after filled cells?
    //assert(*start <= *stop);
}

inline
void Cell::cell(const vector &v, int *start, int *stop) const {
    int _idx = idx(v);
    cell(_idx, start, stop);
}

std::vector<vector> Cell::nb(const vector &v) const {
    std::vector<vector> nb;

    int start, stop;
    cell(v, &start, &stop);

    for (int i = start; i < stop; ++i) {
        nb.push_back(particles->at(i));
    }

    return nb;
}

void Cell::flatten(const std::vector<std::vector<fp> > &dists, const std::vector<std::vector<int> > &indices,
                   fp **flatten_dists, int **flatten_indices,
                   int **offsets, int *size) const {
    int *sizes = new int[particles->size()];

#pragma omp parallel for
    for (int i = 0; i < indices.size(); ++i) {
        sizes[i] = indices[i].size();
    }

    *offsets = new int[particles->size()];
    thrust::exclusive_scan(&sizes[0], &sizes[particles->size()], &(*offsets)[0]);
    *size = (*offsets)[particles->size()-1];

    *flatten_dists = new fp[*size];
    *flatten_indices = new int[*size];

//#pragma omp parallel for
    for (int i = 0; i < particles->size(); ++i) {
        const std::vector<fp> &_d = dists.at(i);
        const std::vector<int> &_i = indices.at(i);
        int start = (*offsets)[i];
        for (int j = 0; j < _d.size(); ++j) {
            (*flatten_dists)[start + j] = _d[j];
            (*flatten_indices)[start + j] = _i[j];
        }
    }
}

void Cell::find_distances(std::vector<std::vector<fp> > &dists,
                          std::vector<std::vector<int> > &indices) const {
    //hopefully resize checks wheather the resize is silly or not
    dists.resize(particles->size());
    indices.resize(particles->size());

#pragma omp parallel for
    for (int ci = 0; ci < w*h; ++ci) {
        int start, stop;
        cell(ci, &start, &stop);
        if (stop <= start) {
            continue;
        }
        candidates_type candidates;
        candidates.reserve(170);
        find_candidates(ci % w, ci / w, candidates);
        for (int i = start; i < stop; ++i) {
            filter_by_radius(i, candidates, dists[i], indices[i]);
        }
    }
}

void Cell::find_candidates(int x, int y, candidates_type &candidates) const {
    int start, stop;

    for (int xo = -1; xo < 2; ++xo) {
        for (int yo = -1; yo < 2; ++yo) {
            if (!idx_ok(x+xo, y+yo)) continue;

            cell((y+yo)*w+x+xo, &start, &stop);
            for (int i = start; i < stop; i++) candidates.push_back(std::make_pair(particles->at(i), i));
        }
    }
}

void Cell::filter_by_radius(int i, const candidates_type &candidates,
                            std::vector<fp> &dists, std::vector<int> &indices) const {

    indices.reserve(30);
    dists.reserve(30);

    fp radius = dx*dx;
    vector v_i = particles->at(i);

    for (int j = 0; j < candidates.size(); ++j) {
        const vector &v_j = candidates[j].first;

        fp r_ij = (v_i - v_j).length_squared();

        if (r_ij > radius) continue;

        dists.push_back(r_ij);
        indices.push_back(candidates[j].second);
    }
}

int Cell::nearest(const vector &v) const {
    int start, stop;
    cell(v, &start, &stop);

    int _nearest = start;
    fp dist = (particles->at(start) - v).length_squared();

    for (int i = start+1; i < stop; ++i) {
        fp ndist = (particles->at(i) - v).length_squared();
        if (ndist < dist) {
            dist = ndist;
            _nearest = i;
        }
    }
    return _nearest;
}
