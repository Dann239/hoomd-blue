// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

/*! \file ParticleFilterUpdater.h
    \brief Declares an updater that recomputes ParticleGroup's from a list of ParticleFilter
    instances.
*/

#ifdef __HIPCC__
#error This header cannot be compiled by nvcc
#endif

#include <memory>
#include <pybind11/pybind11.h>

#include "ParticleGroup.h"
#include "Updater.h"

#pragma once

namespace hoomd
    {
/// Recomputes ParticleGroups of associated filters.
/** The updater takes in a vector of particle filters updates ParticleGroups for each ParticleFilter
 * when triggered.
 * \ingroup updaters
 */
class PYBIND11_EXPORT ParticleFilterUpdater : public Updater
    {
    public:
    /// Constructor
    ParticleFilterUpdater(std::shared_ptr<SystemDefinition> sysdef,
                          std::vector<std::shared_ptr<ParticleGroup>> groups = {});

    /// Destructor
    virtual ~ParticleFilterUpdater();

    /// get the list of groups to update members
    std::vector<std::shared_ptr<ParticleGroup>>& getGroups()
        {
        return m_groups;
        }

    /// Update particle group membership
    virtual void update(uint64_t timestep);

    private:
    std::vector<std::shared_ptr<ParticleGroup>> m_groups; //!< Selected groups to update
    };

namespace detail
    {
/// Export the BoxResizeUpdater to python
void export_ParticleFilterUpdater(pybind11::module& m);

    } // end namespace detail

    } // end namespace hoomd
