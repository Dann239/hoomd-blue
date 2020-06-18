// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

#include "TwoStepRATTLEBDGPU.cuh"
#include "hoomd/VectorMath.h"
#include "hoomd/HOOMDMath.h"

#include "hoomd/RandomNumbers.h"
#include "hoomd/RNGIdentifiers.h"
using namespace hoomd;

#include <assert.h>

inline __device__ Scalar maxNorm(Scalar3 vec, Scalar resid)
    {
    Scalar vec_norm = sqrt(dot(vec,vec));
    Scalar abs_resid = fabs(resid);
    if ( vec_norm > abs_resid) return vec_norm;
    else return abs_resid;
    }

/*! \file TwoSteRATTLEBDGPU.cu
    \brief Defines GPU kernel code for Brownian integration on the GPU. Used by TwoStepRATTLEBDGPU.
*/

//! Takes the second half-step forward in the Langevin integration on a group of particles with
/*! \param d_pos array of particle positions and types
    \param d_vel array of particle positions and masses
    \param d_image array of particle images
    \param box simulation box
    \param d_diameter array of particle diameters
    \param d_tag array of particle tags
    \param d_group_members Device array listing the indices of the members of the group to integrate
    \param nwork Number of group members to process on this GPU
    \param d_net_force Net force on each particle
    \param d_gamma_r List of per-type gamma_rs (rotational drag coeff.)
    \param d_orientation Device array of orientation quaternion
    \param d_torque Device array of net torque on each particle
    \param d_inertia Device array of moment of inertial of each particle
    \param d_angmom Device array of transformed angular momentum quaternion of each particle (see online documentation)
    \param d_gamma List of per-type gammas
    \param n_types Number of particle types in the simulation
    \param use_lambda If true, gamma = lambda * diameter
    \param lambda Scale factor to convert diameter to lambda (when use_lambda is true)
    \param timestep Current timestep of the simulation
    \param seed User chosen random number seed
    \param T Temperature set point
    \param aniso If set true, the system would go through rigid body updates for its orientation
    \param deltaT Amount of real time to step forward in one time step
    \param D Dimensionality of the system
    \param d_noiseless_t If set true, there will be no translational noise (random force)
    \param d_noiseless_r If set true, there will be no rotational noise (random torque)
    \param offset Offset of this GPU into group indices

    This kernel is implemented in a very similar manner to gpu_nve_step_one_kernel(), see it for design details.

    This kernel must be launched with enough dynamic shared memory per block to read in d_gamma
*/
extern "C" __global__
void gpu_rattle_brownian_step_one_kernel(Scalar4 *d_pos,
                                  Scalar4 *d_vel,
                                  int3 *d_image,
                                  const BoxDim box,
                                  const Scalar *d_diameter,
                                  unsigned int *d_rtag,
                                  unsigned int *d_groupTags,
                                  const unsigned int nwork,
                                  const Scalar4 *d_net_force,
                                  const Scalar3 *d_f_brownian,
                                  const Scalar3 *d_gamma_r,
                                  Scalar4 *d_orientation,
                                  Scalar4 *d_torque,
                                  const Scalar3 *d_inertia,
                                  Scalar4 *d_angmom,
                                  const Scalar *d_gamma,
                                  const unsigned int n_types,
                                  const bool use_lambda,
                                  const Scalar lambda,
                                  const unsigned int timestep,
                                  const unsigned int seed,
                                  const Scalar T,
                                  const bool aniso,
                                  const Scalar deltaT,
                                  unsigned int D,
                                  const bool d_noiseless_t,
                                  const bool d_noiseless_r,
                                  const unsigned int offset)
    {
    extern __shared__ char s_data[];

    Scalar3 *s_gammas_r = (Scalar3 *)s_data;
    Scalar *s_gammas = (Scalar *)(s_gammas_r + n_types);

    if (!use_lambda)
        {
        // read in the gamma (1 dimensional array), stored in s_gammas[0: n_type] (Pythonic convention)
        for (int cur_offset = 0; cur_offset < n_types; cur_offset += blockDim.x)
            {
            if (cur_offset + threadIdx.x < n_types)
                s_gammas[cur_offset + threadIdx.x] = d_gamma[cur_offset + threadIdx.x];
            }
        __syncthreads();
        }

    // read in the gamma_r, stored in s_gammas_r[0: n_type], which is s_gamma_r[0:n_type]

    for (int cur_offset = 0; cur_offset < n_types; cur_offset += blockDim.x)
        {
        if (cur_offset + threadIdx.x < n_types)
            s_gammas_r[cur_offset + threadIdx.x] = d_gamma_r[cur_offset + threadIdx.x];
        }
    __syncthreads();

    // determine which particle this thread works on (MEM TRANSFER: 4 bytes)
    int local_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (local_idx < nwork)
        {
        const unsigned int group_idx = local_idx + offset;

        // determine the particle to work on
        unsigned int tag = d_groupTags[group_idx];
        unsigned int idx = d_rtag[tag];

        Scalar4 postype = d_pos[idx];
        Scalar4 vel = d_vel[idx];
        Scalar4 net_force = d_net_force[idx];
        Scalar3 brownian_force = d_f_brownian[tag];
        int3 image = d_image[idx];

        // calculate the magnitude of the random force
        Scalar gamma;
        if (use_lambda)
            {
            // determine gamma from diameter
            gamma = lambda*d_diameter[idx];
            }
        else
            {
            // determine gamma from type
            unsigned int typ = __scalar_as_int(postype.w);
            gamma = s_gammas[typ];
            }
        Scalar deltaT_gamma = deltaT/gamma;


        // compute the random force
        RandomGenerator rng(RNGIdentifier::TwoStepBD, seed, tag, timestep);

        
        Scalar dx = (net_force.x + brownian_force.x) * deltaT_gamma;
        Scalar dy = (net_force.y + brownian_force.y) * deltaT_gamma;
        Scalar dz = (net_force.z + brownian_force.z) * deltaT_gamma;

	    postype.x += dx;
	    postype.y += dy;
	    postype.z += dz;
// particles may have been moved slightly outside the box by the above steps, wrap them back into place
        box.wrap(postype, image);

        // draw a new random velocity for particle j
        Scalar mass = vel.w;
        Scalar sigma = fast::sqrt(T/mass);
        NormalDistribution<Scalar> norm(sigma);
        vel.x = norm(rng);
        vel.y = norm(rng);
        vel.z = norm(rng);

        // write out data
        d_pos[idx] = postype;
        d_vel[idx] = vel;
        d_image[idx] = image;

        // rotational random force and orientation quaternion updates
        if (aniso)
            {
            unsigned int type_r = __scalar_as_int(d_pos[idx].w);

            // gamma_r is stored in the second half of s_gammas a.k.a s_gammas_r
            Scalar3 gamma_r = s_gammas_r[type_r];
            if (gamma_r.x > 0 || gamma_r.y > 0 || gamma_r.z > 0)
                {
                vec3<Scalar> p_vec;
                quat<Scalar> q(d_orientation[idx]);
                vec3<Scalar> t(d_torque[idx]);
                vec3<Scalar> I(d_inertia[idx]);

                // check if the shape is degenerate
                bool x_zero, y_zero, z_zero;
                x_zero = (I.x < EPSILON); y_zero = (I.y < EPSILON); z_zero = (I.z < EPSILON);

                Scalar3 sigma_r = make_scalar3(fast::sqrt(Scalar(2.0)*gamma_r.x*T/deltaT),
                                               fast::sqrt(Scalar(2.0)*gamma_r.y*T/deltaT),
                                               fast::sqrt(Scalar(2.0)*gamma_r.z*T/deltaT));
                if (d_noiseless_r)
                    sigma_r = make_scalar3(0,0,0);

                // original Gaussian random torque
                // Gaussian random distribution is preferred in terms of preserving the exact math
                vec3<Scalar> bf_torque;
                bf_torque.x = NormalDistribution<Scalar>(sigma_r.x)(rng);
                bf_torque.y = NormalDistribution<Scalar>(sigma_r.y)(rng);
                bf_torque.z = NormalDistribution<Scalar>(sigma_r.z)(rng);

                if (x_zero) bf_torque.x = 0;
                if (y_zero) bf_torque.y = 0;
                if (z_zero) bf_torque.z = 0;

                // use the damping by gamma_r and rotate back to lab frame
                // For Future Updates: take special care when have anisotropic gamma_r
                bf_torque = rotate(q, bf_torque);
                if (D < 3)
                    {
                    bf_torque.x = 0;
                    bf_torque.y = 0;
                    t.x = 0;
                    t.y = 0;
                    }

                // do the integration for quaternion
                q += Scalar(0.5) * deltaT * ((t + bf_torque) / vec3<Scalar>(gamma_r)) * q ;
                q = q * (Scalar(1.0) / slow::sqrt(norm2(q)));
                d_orientation[idx] = quat_to_scalar4(q);

                // draw a new random ang_mom for particle j in body frame
                p_vec.x = NormalDistribution<Scalar>(fast::sqrt(T * I.x))(rng);
                p_vec.y = NormalDistribution<Scalar>(fast::sqrt(T * I.y))(rng);
                p_vec.z = NormalDistribution<Scalar>(fast::sqrt(T * I.z))(rng);
                if (x_zero) p_vec.x = 0;
                if (y_zero) p_vec.y = 0;
                if (z_zero) p_vec.z = 0;

                // !! Note this ang_mom isn't well-behaving in 2D,
                // !! because may have effective non-zero ang_mom in x,y

                // store ang_mom quaternion
                quat<Scalar> p = Scalar(2.0) * q * p_vec;
                d_angmom[idx] = quat_to_scalar4(p);
                }
            }
        }
    }

/*! \param d_pos array of particle positions and types
    \param d_vel array of particle positions and masses
    \param d_image array of particle images
    \param box simulation box
    \param d_diameter array of particle diameters
    \param d_tag array of particle tags
    \param d_group_members Device array listing the indices of the members of the group to integrate
    \param group_size Number of members in the group
    \param d_net_force Net force on each particle
    \param d_gamma_r List of per-type gamma_rs (rotational drag coeff.)
    \param d_orientation Device array of orientation quaternion
    \param d_torque Device array of net torque on each particle
    \param d_inertia Device array of moment of inertial of each particle
    \param d_angmom Device array of transformed angular momentum quaternion of each particle (see online documentation)
    \param rattle_langevin.g_args Collected arguments for gpu_brownian_step_one_kernel()
    \param aniso If set true, the system would go through rigid body updates for its orientation
    \param deltaT Amount of real time to step forward in one time step
    \param D Dimensionality of the system
    \param d_noiseless_t If set true, there will be no translational noise (random force)
    \param d_noiseless_r If set true, there will be no rotational noise (random torque)

    This is just a driver for gpu_brownian_step_one_kernel(), see it for details.
*/
cudaError_t gpu_rattle_brownian_step_one(Scalar4 *d_pos,
                                  Scalar4 *d_vel,
                                  int3 *d_image,
                                  const BoxDim& box,
                                  const Scalar *d_diameter,
                                  const unsigned int *d_rtag,
                                  const unsigned int *d_groupTags,
                                  const unsigned int group_size,
                                  const Scalar4 *d_net_force,
                                  const Scalar3 *d_f_brownian,
                                  const Scalar3 *d_gamma_r,
                                  Scalar4 *d_orientation,
                                  Scalar4 *d_torque,
                                  const Scalar3 *d_inertia,
                                  Scalar4 *d_angmom,
                                  const rattle_bd_step_one_args& rattle_bd_args,
                                  const bool aniso,
                                  const Scalar deltaT,
                                  const unsigned int D,
                                  const bool d_noiseless_t,
                                  const bool d_noiseless_r,
                                  const GPUPartition& gpu_partition
                                  )
    {
    unsigned int run_block_size = 256;

    // iterate over active GPUs in reverse, to end up on first GPU when returning from this function
    for (int idev = gpu_partition.getNumActiveGPUs() - 1; idev >= 0; --idev)
        {
        auto range = gpu_partition.getRangeAndSetGPU(idev);

        unsigned int nwork = range.second - range.first;

        // setup the grid to run the kernel
        dim3 grid( (nwork/run_block_size) + 1, 1, 1);
        dim3 threads(run_block_size, 1, 1);

        // run the kernel
        gpu_rattle_brownian_step_one_kernel<<< grid, threads, (unsigned int)(sizeof(Scalar)*rattle_bd_args.n_types + sizeof(Scalar3)*rattle_bd_args.n_types)>>>
                                    (d_pos,
                                     d_vel,
                                     d_image,
                                     box,
                                     d_diameter,
                                     d_rtag,
                                     d_groupTags,
                                     nwork,
                                     d_net_force,
                                     d_f_brownian,
                                     d_gamma_r,
                                     d_orientation,
                                     d_torque,
                                     d_inertia,
                                     d_angmom,
                                     rattle_bd_args.d_gamma,
                                     rattle_bd_args.n_types,
                                     rattle_bd_args.use_lambda,
                                     rattle_bd_args.lambda,
                                     rattle_bd_args.timestep,
                                     rattle_bd_args.seed,
                                     rattle_bd_args.T,
                                     aniso,
                                     deltaT,
                                     D,
                                     d_noiseless_t,
                                     d_noiseless_r,
                                     range.first);
        }

    return cudaSuccess;
    }

extern "C" __global__
void gpu_include_rattle_force_kernel(const Scalar4 *d_pos,
                                  const Scalar4 *d_vel,
                                  const Scalar *d_diameter,
                                  unsigned int *d_rtag,
                                  unsigned int *d_groupTags,
                                  const unsigned int nwork,
                                  Scalar4 *d_net_force,
                                  Scalar4 *d_f_brownian,
                                  Scalar4 *d_net_virial,
                                  const Scalar *d_gamma,
                                  const unsigned int n_types,
                                  const bool use_lambda,
                                  const Scalar lambda,
                                  const unsigned int timestep,
                                  const unsigned int seed,
                                  const Scalar T,
                                  const Scalar eta,
				                  EvaluatorConstraintManifold manifold,
                                  unsigned int net_virial_pitch,
                                  const Scalar deltaT,
                                  const unsigned int offset)
    {
    extern __shared__ char s_data[];

    Scalar3 *s_gammas_r = (Scalar3 *)s_data;
    Scalar *s_gammas = (Scalar *)(s_gammas_r + n_types);

    if (!use_lambda)
        {
        // read in the gamma (1 dimensional array), stored in s_gammas[0: n_type] (Pythonic convention)
        for (int cur_offset = 0; cur_offset < n_types; cur_offset += blockDim.x)
            {
            if (cur_offset + threadIdx.x < n_types)
                s_gammas[cur_offset + threadIdx.x] = d_gamma[cur_offset + threadIdx.x];
            }
        __syncthreads();
        }

    // read in the gamma_r, stored in s_gammas_r[0: n_type], which is s_gamma_r[0:n_type]

    for (int cur_offset = 0; cur_offset < n_types; cur_offset += blockDim.x)
        {
        if (cur_offset + threadIdx.x < n_types)
            s_gammas_r[cur_offset + threadIdx.x] = d_gamma_r[cur_offset + threadIdx.x];
        }
    __syncthreads();

    // determine which particle this thread works on (MEM TRANSFER: 4 bytes)
    int local_idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (local_idx < nwork)
        {
        const unsigned int group_idx = local_idx + offset;

        // determine the particle to work on
        unsigned int tag = d_groupTags[group_idx];
        unsigned int idx = d_rtag[tag];

        Scalar4 postype = d_pos[idx];
        Scalar4 vel = d_vel[idx];
        Scalar4 net_force = d_net_force[idx];
        Scalar3 brownian_force = d_f_brownian[tag];

        Scalar virial0 = d_net_virial[0*net_virial_pitch+idx];
        Scalar virial1 = d_net_virial[1*net_virial_pitch+idx];
        Scalar virial2 = d_net_virial[2*net_virial_pitch+idx];
        Scalar virial3 = d_net_virial[3*net_virial_pitch+idx];
        Scalar virial4 = d_net_virial[4*net_virial_pitch+idx];
        Scalar virial5 = d_net_virial[5*net_virial_pitch+idx];

        // calculate the magnitude of the random force
        Scalar gamma;
        if (use_lambda)
            {
            // determine gamma from diameter
            gamma = lambda*d_diameter[idx];
            }
        else
            {
            // determine gamma from type
            unsigned int typ = __scalar_as_int(postype.w);
            gamma = s_gammas[typ];
            }
        Scalar deltaT_gamma = deltaT/gamma;


        // compute the random force
        RandomGenerator rng(RNGIdentifier::TwoStepBD, seed, ptag, timestep);

        
	    Scalar3 next_pos;
	    next_pos.x = postype.x;
	    next_pos.y = postype.y;
	    next_pos.z = postype.z;
        Scalar3 normal = manifold.evalNormal(next_pos);

        Scalar rx, ry, rz, coeff;

	    if (T > 0)
	        {
	    	UniformDistribution<Scalar> uniform(Scalar(-1), Scalar(1));
	    	rx = uniform(rng);
	    	ry = uniform(rng);
	    	rz = uniform(rng);

	    	Scalar3 proj = normal;
	    	Scalar proj_norm = 1.0/fast::sqrt(proj.x*proj.x+proj.y*proj.y+proj.z*proj.z);
	    	proj.x *= proj_norm;
	    	proj.y *= proj_norm;
	    	proj.z *= proj_norm;

	    	Scalar proj_r = rx*proj.x + ry*proj.y + rz*proj.z;

	    	rx = rx - proj_r*proj.x;
	    	ry = ry - proj_r*proj.y;
	    	rz = rz - proj_r*proj.z;
	    
                    // compute the bd force (the extra factor of 3 is because <rx^2> is 1/3 in the uniform -1,1 distribution
                    // it is not the dimensionality of the system
                    coeff = fast::sqrt(Scalar(6.0)*T/deltaT_gamma);
                    if (d_noiseless_t)
                        coeff = Scalar(0.0);
	        }
	    else
	        {
               	rx = 0;
               	ry = 0;
               	rz = 0;
               	coeff = 0;
	        }


            brownian_force.x = rx*coeff;
            brownian_force.y = ry*coeff;
            brownian_force.z = rz*coeff;

            // update position

	        Scalar mu = 0;

                unsigned int maxiteration = 10;
	        Scalar inv_alpha = -deltaT_gamma;
	        inv_alpha = Scalar(1.0)/inv_alpha;

	        Scalar3 residual;
	        Scalar resid;
	        unsigned int iteration = 0;

	        do
	        {
	            iteration++;
	            residual.x = postype.x - next_pos.x + (net_force.x + brownian_force.x - mu*normal.x) * deltaT_gamma;
	            residual.y = postype.y - next_pos.y + (net_force.y + brownian_force.y - mu*normal.y) * deltaT_gamma;
	            residual.z = postype.z - next_pos.z + (net_force.z + brownian_force.z - mu*normal.z) * deltaT_gamma;
	            resid = manifold.implicit_function(next_pos);

                    Scalar3 next_normal =  manifold.evalNormal(next_pos);


	            Scalar nndotr = dot(next_normal,residual);
	            Scalar nndotn = dot(next_normal,normal);
	            Scalar beta = (resid + nndotr)/nndotn;

                    next_pos.x = next_pos.x - beta*normal.x + residual.x;   
                    next_pos.y = next_pos.y - beta*normal.y + residual.y;   
                    next_pos.z = next_pos.z - beta*normal.z + residual.z;
	            mu = mu - beta*inv_alpha;
	         
	        } while (maxNorm(residual,resid) > eta && iteration < maxiteration );
    
            net_force.x -= mu*normal.x;
            net_force.y -= mu*normal.y;
            net_force.z -= mu*normal.z;

        virial0 -= mu*normal.x*pos.x;
        virial1 -= 0.5*mu*(normal.x*pos.y+normal.y*pos.x);
        virial2 -= 0.5*mu*(normal.x*pos.z+normal.z*pos.x);
        virial3 -= mu*normal.y*pos.y;
        virial4 -= 0.5*mu*(normal.y*pos.z+normal.z*pos.y);
        virial5 -= mu*normal.z*pos.z;

        d_f_brownian[tag] = brownian_force;

        d_net_force[idx] = net_force;
        d_net_virial[0*net_virial_pitch+idx] = virial0;
        d_net_virial[1*net_virial_pitch+idx] = virial1;
        d_net_virial[2*net_virial_pitch+idx] = virial2;
        d_net_virial[3*net_virial_pitch+idx] = virial3;
        d_net_virial[4*net_virial_pitch+idx] = virial4;
        d_net_virial[5*net_virial_pitch+idx] = virial5;

        }
    }

cudaError_t gpu_include_rattle_force(const Scalar4 *d_pos,
                                  const Scalar4 *d_vel,
                                  Scalar4 *d_net_force,
                                  Scalar3 *d_f_brownian,
                                  Scalar *d_net_virial,
                                  const Scalar *d_diameter,
                                  const unsigned int *d_rtag,
                                  const unsigned int *d_groupTags,
                                  const unsigned int group_size,
                                  Scalar4 *d_angmom,
                                  const rattle_bd_step_one_args& rattle_bd_args,
			                      EvaluatorConstraintManifold manifold,
                                  unsigned int net_virial_pitch,
                                  const Scalar deltaT,
                                  const GPUPartition& gpu_partition
                                  )
    {
    unsigned int run_block_size = 256;

    // iterate over active GPUs in reverse, to end up on first GPU when returning from this function
    for (int idev = gpu_partition.getNumActiveGPUs() - 1; idev >= 0; --idev)
        {
        auto range = gpu_partition.getRangeAndSetGPU(idev);

        unsigned int nwork = range.second - range.first;

        // setup the grid to run the kernel
        dim3 grid( (nwork/run_block_size) + 1, 1, 1);
        dim3 threads(run_block_size, 1, 1);

        // run the kernel
        gpu_include_rattle_force_kernel<<< grid, threads, (unsigned int)(sizeof(Scalar)*rattle_bd_args.n_types + sizeof(Scalar3)*rattle_bd_args.n_types)>>>
                                    (d_pos,
                                     d_vel,
                                     d_net_force,
                                     d_net_brownian,
                                     d_net_virial,
                                     d_diameter,
                                     d_rtag,
                                     d_groupTags,
                                     nwork,
                                     rattle_bd_args.d_gamma,
                                     rattle_bd_args.n_types,
                                     rattle_bd_args.use_lambda,
                                     rattle_bd_args.lambda,
                                     rattle_bd_args.timestep,
                                     rattle_bd_args.seed,
                                     rattle_bd_args.T,
			                         EvaluatorConstraintManifold manifold,
                                     rattle_langevin_args.eta,
                                     net_virial_pitch,
                                     deltaT,
                                     range.first);
        }

    return cudaSuccess;
    }
