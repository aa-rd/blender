// Copyright Matt Overby 2020.
// Distributed under the MIT License.

#include "admmpd_sdf.h"
#include "admmpd_geom.h"
#include <vector>
#include "sdfgen/makelevelset3.h"

namespace admmpd {
using namespace Eigen;

template<typename T>
SDF<T>::SDF() : sdf_dx(-1)
{}


template<typename T>
bool SDF<T>::generate(const MatrixXT *V_, const Eigen::MatrixXi *F_, T dx_frac)
{
    const MatrixXT &V = *V_;
    const MatrixXi &F = *F_;
    
    sdf_dx = -1;
	sdf.clear();
    aabb.setEmpty();

    int nv = V.rows();
    if (nv == 0)
    {
        return false;
    }

	std::vector<sdfgen::Vec3f> vertList;
    for (int i=0; i<nv; ++i)
    {
        aabb.extend(V.row(i).transpose());
    	vertList.emplace_back();
		for(int j=0; j<3; ++j)
			vertList.back()[j]=(float)V(i,j);
    }

    // Consider k=n^(1/3) rule, with grid
    // length = k and n = number of primitives.
    // Grid cells should be larger than the largest primitive.

	VecType sizes = aabb.sizes();
	T grid_dx = sizes.maxCoeff()*dx_frac;
    sdf_dx = grid_dx * 0.5;
    sdfgen::Vec3f min_box = sdfgen::Vec3f(aabb.min()[0], aabb.min()[1], aabb.min()[2]);
    sdfgen::Vec3f max_box = sdfgen::Vec3f(aabb.max()[0], aabb.max()[1], aabb.max()[2]);
    sdfgen::Vec3ui sdf_sizes = sdfgen::Vec3ui((max_box-min_box)/sdf_dx)+sdfgen::Vec3ui(1,1,1);

    std::vector<sdfgen::Vec3ui> faceList;
	int nf = F.rows();
	for (int i=0; i<nf; ++i)
	{
		faceList.emplace_back();
		for(int j=0; j<3; ++j)
			faceList.back()[j]=(unsigned int)F(i,j);
	}

    sdfgen::make_level_set3(
        faceList, vertList, min_box, sdf_dx,
        sdf_sizes[0], sdf_sizes[1], sdf_sizes[2],
        sdf);

    update_mapping(V_,F_);

    return true;
} // end generate SDF

template<typename T>
void SDF<T>::update_mapping(
        const MatrixXT *V_,
        const Eigen::MatrixXi *F_)
{
    V_map.clear();
    F_map.clear();

    const MatrixXT &V = *V_;
    const MatrixXi &F = *F_; 
    int nv = V.rows();
    int nf = F.rows();
    typedef std::unordered_map<int,std::set<int> >::iterator it_type;
    Vector3i max_cell(sdf.ni, sdf.ni, sdf.nk);

    int iters = std::max(nv, nf);
    for (int i=0; i<iters; ++i)
    {
        if (i < nv)
        {
            VecType v = V.row(i);
            Vector3i vi = index(v);
            int idx = vi[0]+sdf.ni*(vi[1]+sdf.nj*vi[2]);
            it_type it = V_map.find(idx);
            if (it == V_map.end())
                it = V_map.emplace(std::make_pair(idx, std::set<int>())).first;
            it->second.emplace(i);
        }
        if (i < nf)
        {
            RowVector3i f = F.row(i);
            VecType v0 = V.row(f[0]);
            VecType v1 = V.row(f[1]);
            VecType v2 = V.row(f[2]);
            AlignedBox<T,3> f_box;
            f_box.extend(v0);
            f_box.extend(v1);
            f_box.extend(v2);
            f_box.extend(f_box.min()-VecType::Ones()*1e-10);
            f_box.extend(f_box.min()+VecType::Ones()*1e-10);
            std::vector<int> cells;
            get_cells(f_box.min(), f_box.max(), cells);
            int nc = cells.size();
            if (nc==0)
                throw std::runtime_error("admmpd::SDF Error: Problem with face mapping");
            for (int j=0; j<nc; ++j)
            {
                const int &idx = cells[j];
                it_type it = F_map.find(idx);
                if (it == F_map.end())
                    it = F_map.emplace(std::make_pair(idx, std::set<int>())).first;
                it->second.emplace(i);     
            }
        }
    } // end loop
} // end update mapping

template<typename T>
Eigen::Vector3i SDF<T>::index(const VecType &x) const
{
    VecType x_min = x - aabb.min();
    return Vector3i(
        std::min(std::max(0,(int)std::round((x_min[0])/sdf_dx)),sdf.ni),
        std::min(std::max(0,(int)std::round((x_min[1])/sdf_dx)),sdf.nj),
        std::min(std::max(0,(int)std::round((x_min[2])/sdf_dx)),sdf.nk)
    );
}

template<typename T>
T SDF<T>::sample(const VecType &x) const
{
    if (sdf_dx < 0)
        return 0;

    Vector3i x_inds = index(x);
    return sdf(x_inds[0],x_inds[1],x_inds[2]);

}

template<typename T>
void SDF<T>::vertices(const Eigen::Vector3i &ind, std::vector<int> &v) const
{
    int idx = ind[0]+sdf.ni*(ind[1]+sdf.nj*ind[2]);
    std::unordered_map<int,std::set<int> >::const_iterator it = V_map.find(idx);
    if (it == V_map.end())
        return;
    v.insert(v.end(), it->second.begin(), it->second.end());
}

template<typename T>
void SDF<T>::faces(const Eigen::Vector3i &ind, std::vector<int> &f) const
{
    int idx = ind[0]+sdf.ni*(ind[1]+sdf.nj*ind[2]);
    std::unordered_map<int,std::set<int> >::const_iterator it = F_map.find(idx);
    if (it == F_map.end())
        return;
    f.insert(f.end(), it->second.begin(), it->second.end());
}

template <typename T>
void SDF<T>::get_cells(
        const VecType &bmin, const VecType &bmax,
	    std::vector<int> &cells_inds) const
{
    Vector3i bmin_cell = index(bmin);
    Vector3i bmax_cell = index(bmax);
    Vector3i cell(0,0,0);
	for (cell[0]=bmin_cell[0]; cell[0]<=bmax_cell[0]; ++cell[0])
    {
		for (cell[1]=bmin_cell[1]; cell[1]<=bmax_cell[1]; ++cell[1])
        {
            for (cell[1]=bmin_cell[1]; cell[1]<=bmax_cell[1]; ++cell[1])
            {
                int idx = cell[0]+sdf.ni*(cell[1]+sdf.nj*cell[2]);
                cells_inds.emplace_back(idx);
            }
        }
    }
}

template class admmpd::SDF<double>;
template class admmpd::SDF<float>;

} // namespace admmpd