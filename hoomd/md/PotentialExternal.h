// Copyright (c) 2009-2018 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: jglaser

#include <memory>
#include "hoomd/ForceCompute.h"

/*! \file PotentialExternal.h
    \brief Declares a class for computing an external force field
*/

#ifdef NVCC
#error This header cannot be compiled by nvcc
#endif

#include <hoomd/extern/pybind/include/pybind11/pybind11.h>

#ifndef __POTENTIAL_EXTERNAL_H__
#define __POTENTIAL_EXTERNAL_H__

//! Applys an external force to particles based on position
/*! \ingroup computes
*/
template<class evaluator>
class PotentialExternal: public ForceCompute
    {
    public:
        //! Constructs the compute
        PotentialExternal<evaluator>(std::shared_ptr<SystemDefinition> sysdef,
                                     const std::string& log_suffix="");
        virtual ~PotentialExternal<evaluator>();

        //! type of external potential parameters
        typedef typename evaluator::param_type param_type;
        typedef typename evaluator::field_type field_type;

        //! Sets parameters of the evaluator
        void setParams(unsigned int type, param_type params);
        void setField(field_type field);

        //! Gets python output for field
        void updateFieldPy(pybind11::object field_py);

        //! Returns a list of log quantities this compute calculates
        virtual std::vector< std::string > getProvidedLogQuantities();

        //! Calculates the requested log value and returns it
        virtual Scalar getLogValue(const std::string& quantity, unsigned int timestep);

    protected:

        GPUArray<param_type>    m_params;       //!< Array of per-type parameters
        std::string             m_log_name;     //!< Cached log name
        GPUArray<field_type>    m_field;        //!< Array of field parameters
        bool                    m_rescale;      //!< Flag to rescale the system for box changes
        BoxDim                  m_old_box;      //!< Stores previous BoxDim, used for rescaling

        //! Actually compute the forces
        virtual void computeForces(unsigned int timestep);

        //! Method to be called when number of types changes
        virtual void slotNumTypesChange()
            {
            // skip the reallocation if the number of types does not change
            // this keeps old parameters when restoring a snapshot
            // it will result in invalid coeficients if the snapshot has a different type id -> name mapping
            if (m_pdata->getNTypes() == m_params.getNumElements())
                return;

            // reallocate parameter array
            GPUArray<param_type> params(m_pdata->getNTypes(), m_exec_conf);
            m_params.swap(params);
            }

        //!Box Change update
        virtual void slotBoxChange()
            {
            m_rescale= true;
            }
   };

/*! Constructor
    \param sysdef system definition
    \param log_suffix Name given to this instance of the force
*/
template<class evaluator>
PotentialExternal<evaluator>::PotentialExternal(std::shared_ptr<SystemDefinition> sysdef,
                         const std::string& log_suffix)
    : ForceCompute(sysdef)
    {
    m_log_name = std::string("external_") + evaluator::getName() + std::string("_energy") + log_suffix;

    GPUArray<param_type> params(m_pdata->getNTypes(), m_exec_conf);
    m_params.swap(params);

    GPUArray<field_type> field(1, m_exec_conf);
    m_field.swap(field);

    //set no rescale
    m_rescale = false;
    m_old_box = m_pdata->getGlobalBox();

    // connect to the ParticleData to receive notifications when the maximum number of particles changes
    m_pdata->getNumTypesChangeSignal().template connect<PotentialExternal<evaluator>, &PotentialExternal<evaluator>::slotNumTypesChange>(this);
    // connect to the boxchange signal to recieve notifications when field parameter should be rescaled
    m_pdata->getBoxChangeSignal().template connect<PotentialExternal<evaluator>, &PotentialExternal<evaluator>::slotBoxChange>(this);
    }

/*! Destructor
*/
template<class evaluator>
PotentialExternal<evaluator>::~PotentialExternal()
    {
    m_pdata->getNumTypesChangeSignal().template disconnect<PotentialExternal<evaluator>, &PotentialExternal<evaluator>::slotNumTypesChange>(this);
    m_pdata->getBoxChangeSignal().template disconnect<PotentialExternal<evaluator>, &PotentialExternal<evaluator>::slotBoxChange>(this);
    }

/*! PotentialExternal provides
    - \c external_"name"_energy
*/
template<class evaluator>
std::vector< std::string > PotentialExternal<evaluator>::getProvidedLogQuantities()
    {
    std::vector<std::string> list;
    list.push_back(m_log_name);
    return list;
    }

/*! \param quantity Name of the log value to get
    \param timestep Current timestep of the simulation
*/
template<class evaluator>
Scalar PotentialExternal<evaluator>::getLogValue(const std::string& quantity, unsigned int timestep)
    {
    if (quantity == m_log_name)
        {
        compute(timestep);
        return calcEnergySum();
        }
    else
        {
        this->m_exec_conf->msg->error() << "external." << evaluator::getName() << ": " << quantity << " is not a valid log quantity" << std::endl;
        throw std::runtime_error("Error getting log value");
        }
    }

/*! Computes the specified constraint forces
    \param timestep Current timestep
*/
template<class evaluator>
void PotentialExternal<evaluator>::computeForces(unsigned int timestep)
    {

    if (m_prof) m_prof->push("PotentialExternal");

    assert(m_pdata);
    // access the particle data arrays
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_vel(m_pdata->getVelocities(), access_location::host, access_mode::read);

    ArrayHandle<Scalar4> h_force(m_force,access_location::host, access_mode::overwrite);
    ArrayHandle<Scalar> h_virial(m_virial,access_location::host, access_mode::overwrite);
    ArrayHandle<Scalar> h_diameter(m_pdata->getDiameters(), access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_charge(m_pdata->getCharges(), access_location::host, access_mode::read);

    ArrayHandle<param_type> h_params(m_params, access_location::host, access_mode::read);

    //rescale external parameters if needed for box changes
    const BoxDim& box = m_pdata->getGlobalBox();
    if(evaluator::needsFieldRescale() and m_rescale)
        {
        ArrayHandle<field_type> h_field(m_field, access_location::host, access_mode::readwrite);
        field_type& field = *(h_field.data);

        evaluator::rescaleField(field, box, m_old_box);
        m_old_box = box;
        m_rescale = false;
        }

    ArrayHandle<field_type> h_field(m_field, access_location::host, access_mode::read);
    const field_type& field = *(h_field.data);

    unsigned int nparticles = m_pdata->getN();

    // Zero data for force calculation.
    memset((void*)h_force.data,0,sizeof(Scalar4)*m_force.getNumElements());
    memset((void*)h_virial.data,0,sizeof(Scalar)*m_virial.getNumElements());

   // there are enough other checks on the input data: but it doesn't hurt to be safe
    assert(h_force.data);
    assert(h_virial.data);

    // for each of the particles
    for (unsigned int idx = 0; idx < nparticles; idx++)
        {
        // get the current particle properties
        Scalar3 X = make_scalar3(h_pos.data[idx].x, h_pos.data[idx].y, h_pos.data[idx].z);
        Scalar3 V = make_scalar3(h_vel.data[idx].x, h_vel.data[idx].y, h_vel.data[idx].z);
        V *= m_deltaT;
        unsigned int type = __scalar_as_int(h_pos.data[idx].w);
        Scalar3 F;
        Scalar energy;
        Scalar virial[6];

        param_type params = h_params.data[type];
        evaluator eval(X,V, box, params, field);

        if (evaluator::needsDiameter())
            {
            Scalar di = h_diameter.data[idx];
            eval.setDiameter(di);
            }
        if (evaluator::needsCharge())
            {
            Scalar qi = h_charge.data[idx];
            eval.setCharge(qi);
            }
        eval.evalForceEnergyAndVirial(F, energy, virial);

        // apply the constraint force
        h_force.data[idx].x = F.x;
        h_force.data[idx].y = F.y;
        h_force.data[idx].z = F.z;
        h_force.data[idx].w = energy;
        for (int k = 0; k < 6; k++)
            h_virial.data[k*m_virial_pitch+idx]  = virial[k];

        }

    if (m_prof)
        m_prof->pop();
    }

//! Set the parameters for this potential
/*! \param type type for which to set parameters
    \param params value of parameters
*/
template<class evaluator>
void PotentialExternal<evaluator>::setParams(unsigned int type, param_type params)
    {
    if (type >= m_pdata->getNTypes())
        {
        this->m_exec_conf->msg->error() << "external.periodic: Trying to set external potential params for a non existant type! "
                                        << type << std::endl;
        throw std::runtime_error("Error setting parameters in PotentialExternal");
        }

    ArrayHandle<param_type> h_params(m_params, access_location::host, access_mode::readwrite);
    h_params.data[type] = params;
    }

template<class evaluator>
void PotentialExternal<evaluator>::setField(field_type field)
    {
    ArrayHandle<field_type> h_field(m_field, access_location::host, access_mode::overwrite);
    *(h_field.data) = field;
    }

template<class evaluator>
void PotentialExternal<evaluator>::updateFieldPy(pybind11::object field_py)
    {
    ArrayHandle<field_type> h_field(m_field, access_location::host, access_mode::read);
    field_type& field = *(h_field.data);
    evaluator::updateFieldPy(field, field_py);
    }

//! Export this external potential to python
/*! \param name Name of the class in the exported python module
    \tparam T Class type to export. \b Must be an instantiated PotentialExternal class template.
*/
template < class T >
void export_PotentialExternal(pybind11::module& m, const std::string& name)
    {
    pybind11::class_<T, std::shared_ptr<T> >(m, name.c_str(), pybind11::base<ForceCompute>())
                  .def(pybind11::init< std::shared_ptr<SystemDefinition>, const std::string& >())
                  .def("setParams", &T::setParams)
                  .def("setField", &T::setField)
                  .def("updateFieldPy", &T::updateFieldPy)
                  ;
    }

#endif
