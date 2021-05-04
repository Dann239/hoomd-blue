// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: jglaser

/*! \file AllDriverAnisoPotentialPairGPU.cu
    \brief Defines the driver functions for computing all types of anisotropic pair forces on the GPU
*/

#ifndef __ALL_DRIVER_ANISO_POTENTIAL_PAIR_GPU_CUH__
#define __ALL_DRIVER_ANISO_POTENTIAL_PAIR_GPU_CUH__

#include "AnisoPotentialPairGPU.cuh"
#include "EvaluatorPairALJ.h"
#include "EvaluatorPairGB.h"
#include "EvaluatorPairDipole.h"

//! Compute dipole forces and torques on the GPU with EvaluatorPairDipole

hipError_t __attribute__((visibility("default")))
gpu_compute_pair_aniso_forces_alj_2d(const a_pair_args_t&,
            const EvaluatorPairALJ<2>::param_type*,
            const EvaluatorPairALJ<2>::shape_type*);

hipError_t __attribute__((visibility("default")))
gpu_compute_pair_aniso_forces_alj_3d(const a_pair_args_t&,
            const EvaluatorPairALJ<3>::param_type*,
            const EvaluatorPairALJ<3>::shape_type*);

hipError_t __attribute__((visibility("default")))
gpu_compute_pair_aniso_forces_gb(const a_pair_args_t&,
            const EvaluatorPairGB::param_type*,
            const EvaluatorPairGB::shape_type*);

hipError_t __attribute__((visibility("default")))
gpu_compute_pair_aniso_forces_dipole(const a_pair_args_t&,
            const EvaluatorPairDipole::param_type*,
            const EvaluatorPairDipole::shape_type*);

#endif
