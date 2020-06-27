// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

#pragma once

#ifdef ENABLE_HIP

#include "hoomd/hpmc/IntegratorHPMCMono.h"
#include "hoomd/hpmc/IntegratorHPMCMonoGPUTypes.cuh"
#include "hoomd/hpmc/IntegratorHPMCMonoGPUDepletantsTypes.cuh"
#include "hoomd/hpmc/IntegratorHPMCMonoGPUDepletantsAuxilliaryTypes.cuh"
#include "SATSolver.cuh"

#include "hoomd/Autotuner.h"
#include "hoomd/GlobalArray.h"
#include "hoomd/GPUVector.h"
#include "hoomd/RandomNumbers.h"
#include "hoomd/RNGIdentifiers.h"

#include "hoomd/GPUPartition.cuh"

#include <hip/hip_runtime.h>

#ifdef ENABLE_MPI
#include <mpi.h>
#include "hoomd/MPIConfiguration.h"
#endif

/*! \file IntegratorHPMCMonoGPU.h
    \brief Defines the template class for HPMC on the GPU
    \note This header cannot be compiled by nvcc
*/

#ifdef __HIPCC__
#error This header cannot be compiled by nvcc
#endif

#include <pybind11/pybind11.h>

namespace hpmc
{

namespace detail
{

//! Helper class to manage shuffled update orders in a GlobalVector
/*! Stores an update order from 0 to N-1, inclusive, and can be resized. shuffle() shuffles the order of elements
    to a new random permutation. operator [i] gets the index of the item at order i in the current shuffled sequence.

    NOTE: this should supersede UpdateOrder

    \note we use GPUArrays instead of GlobalArrays currently to allow host access to the shuffled order without an
          unnecessary hipDeviceSynchronize()

    \ingroup hpmc_data_structs
*/
class UpdateOrderGPU
    {
    public:
        //! Constructor
        /*! \param seed Random number seed
            \param N number of integers to shuffle
        */
        UpdateOrderGPU(std::shared_ptr<const ExecutionConfiguration> exec_conf, unsigned int seed, unsigned int N=0)
            : m_seed(seed), m_is_reversed(false), m_update_order(exec_conf), m_reverse_update_order(exec_conf)
            {
            resize(N);
            }

        //! Resize the order
        /*! \param N new size
            \post The order is 0, 1, 2, ... N-1
        */
        void resize(unsigned int N)
            {
            if (!N || N == m_update_order.size())
                return;

            // initialize the update order
            m_update_order.resize(N);
            m_reverse_update_order.resize(N);

            ArrayHandle<unsigned int> h_update_order(m_update_order, access_location::host, access_mode::overwrite);
            ArrayHandle<unsigned int> h_reverse_update_order(m_reverse_update_order, access_location::host, access_mode::overwrite);

            for (unsigned int i = 0; i < N; i++)
                {
                h_update_order.data[i] = i;
                h_reverse_update_order.data[i] = N - i - 1;
                }
            m_is_reversed = false;
            }

        //! Shuffle the order
        /*! \param timestep Current timestep of the simulation
            \note \a timestep is used to seed the RNG, thus assuming that the order is shuffled only once per
            timestep.
        */
        void shuffle(unsigned int timestep, unsigned int select = 0)
            {
            hoomd::RandomGenerator rng(hoomd::RNGIdentifier::HPMCMonoShuffle, m_seed, timestep, select);

            // reverse the order with 1/2 probability
            m_is_reversed = hoomd::UniformIntDistribution(1)(rng);
            }

        //! Access element of the shuffled order
        unsigned int operator[](unsigned int i)
            {
            const GlobalVector<unsigned int>& update_order = m_is_reversed ? m_reverse_update_order : m_update_order;
            ArrayHandle<unsigned int> h_update_order(update_order, access_location::host, access_mode::read);
            return h_update_order.data[i];
            }

        //! Access the underlying GlobalVector
        const GlobalVector<unsigned int> & get() const
            {
            if (m_is_reversed)
                return m_reverse_update_order;
            else
                return m_update_order;
            }

    private:
        unsigned int m_seed;                               //!< Random number seed
        bool m_is_reversed;                                //!< True if order is reversed
        GlobalVector<unsigned int> m_update_order;            //!< Update order
        GlobalVector<unsigned int> m_reverse_update_order;    //!< Inverse permutation
    };

} // end namespace detail

//! Template class for HPMC update on the GPU
/*!
    \ingroup hpmc_integrators
*/
template< class Shape >
class IntegratorHPMCMonoGPU : public IntegratorHPMCMono<Shape>
    {
    public:
        //! Construct the integrator
        IntegratorHPMCMonoGPU(std::shared_ptr<SystemDefinition> sysdef,
                              std::shared_ptr<CellList> cl,
                              unsigned int seed);
        //! Destructor
        virtual ~IntegratorHPMCMonoGPU();

        //! Set autotuner parameters
        /*! \param enable Enable/disable autotuning
            \param period period (approximate) in time steps when returning occurs
        */
        virtual void setAutotunerParams(bool enable, unsigned int period)
            {
            m_tuner_update_pdata->setPeriod(period*this->m_nselect);
            m_tuner_update_pdata->setEnabled(enable);

            m_tuner_moves->setPeriod(period*this->m_nselect);
            m_tuner_moves->setEnabled(enable);

            m_tuner_narrow->setPeriod(period*this->m_nselect);
            m_tuner_narrow->setEnabled(enable);

            if (this->m_patch && !this->m_patch_log)
                {
                this->m_patch->setAutotunerParams(enable,
                    period*this->m_nselect);
                }

            m_tuner_depletants->setPeriod(period*this->m_nselect);
            m_tuner_depletants->setEnabled(enable);

            m_tuner_excell_block_size->setPeriod(period);
            m_tuner_excell_block_size->setEnabled(enable);

            m_tuner_dependencies->setPeriod(period*this->m_nselect);
            m_tuner_dependencies->setEnabled(enable);

            m_tuner_mem->setPeriod(period*this->m_nselect);
            m_tuner_mem->setEnabled(enable);

            m_tuner_inequalities->setPeriod(period*this->m_nselect);
            m_tuner_inequalities->setEnabled(enable);

            m_tuner_sat->setPeriod(period*this->m_nselect);
            m_tuner_sat->setEnabled(enable);

            m_tuner_num_depletants->setPeriod(period*this->m_nselect);
            m_tuner_num_depletants->setEnabled(enable);

            m_tuner_num_depletants_ntrial->setPeriod(period*this->m_nselect);
            m_tuner_num_depletants_ntrial->setEnabled(enable);

            m_tuner_depletants_phase1->setPeriod(period*this->m_nselect);
            m_tuner_depletants_phase1->setEnabled(enable);

            m_tuner_depletants_phase2->setPeriod(period*this->m_nselect);
            m_tuner_depletants_phase2->setEnabled(enable);

            m_tuner_depletants_accept->setPeriod(period*this->m_nselect);
            m_tuner_depletants_accept->setEnabled(enable);
            }

        //! Enable deterministic simulations
        virtual void setDeterministic(bool deterministic)
            {
            this->m_exec_conf->msg->notice(2) << "hpmc: Sorting cell list to enable deterministic simulations." << std::endl;
            m_cl->setSortCellList(deterministic);
            }

        //! Method called when numbe of particle types changes
        virtual void slotNumTypesChange();

        //! Take one timestep forward
        virtual void update(unsigned int timestep);

        #ifdef ENABLE_MPI
        void setNtrialCommunicator(std::shared_ptr<MPIConfiguration> mpi_conf)
            {
            m_ntrial_comm = mpi_conf;
            }
        #endif

        #ifdef ENABLE_MPI
        void setParticleCommunicator(std::shared_ptr<MPIConfiguration> mpi_conf)
            {
            m_particle_comm = mpi_conf;
            }
        #endif

        virtual std::vector<hpmc_implicit_counters_t> getImplicitCounters(unsigned int mode=0);

    protected:
        std::shared_ptr<CellList> m_cl;                      //!< Cell list
        uint3 m_last_dim;                                    //!< Dimensions of the cell list on the last call to update
        unsigned int m_last_nmax;                            //!< Last cell list NMax value allocated in excell

        GlobalArray<unsigned int> m_excell_idx;              //!< Particle indices in expanded cells
        GlobalArray<unsigned int> m_excell_size;             //!< Number of particles in each expanded cell
        Index2D m_excell_list_indexer;                       //!< Indexer to access elements of the excell_idx list

        std::unique_ptr<Autotuner> m_tuner_moves;            //!< Autotuner for proposing moves
        std::unique_ptr<Autotuner> m_tuner_narrow;           //!< Autotuner for the narrow phase
        std::unique_ptr<Autotuner> m_tuner_update_pdata;    //!< Autotuner for the update step group and block sizes
        std::unique_ptr<Autotuner> m_tuner_excell_block_size;  //!< Autotuner for excell block_size
        std::unique_ptr<Autotuner> m_tuner_dependencies;     //!< Autotuner for excell block_size
        std::unique_ptr<Autotuner> m_tuner_mem;              //!< Autotuner simply for resetting the memory for the solver
        std::unique_ptr<Autotuner> m_tuner_inequalities;     //!< Autotuner for inequality preprocessing
        std::unique_ptr<Autotuner> m_tuner_sat;              //!< Autotuner for the SAT solver
        std::unique_ptr<Autotuner> m_tuner_depletants;       //!< Autotuner for inserting depletants
        std::unique_ptr<Autotuner> m_tuner_num_depletants;   //!< Autotuner for calculating number of depletants
        std::unique_ptr<Autotuner> m_tuner_num_depletants_ntrial;   //!< Autotuner for calculating number of depletants with ntrial
        std::unique_ptr<Autotuner> m_tuner_depletants_phase1;//!< Tuner for depletants with ntrial, phase 1 kernel
        std::unique_ptr<Autotuner> m_tuner_depletants_phase2;//!< Tuner for depletants with ntrial, phase 2 kernel
        std::unique_ptr<Autotuner> m_tuner_depletants_accept;//!< Tuner for depletants with ntrial, acceptance kernel

        GlobalArray<Scalar4> m_trial_postype;                 //!< New positions (and type) of particles
        GlobalArray<Scalar4> m_trial_orientation;             //!< New orientations
        GlobalArray<Scalar4> m_trial_vel;                     //!< New velocities (auxilliary variables)
        GlobalArray<unsigned int> m_trial_move_type;          //!< Flags to indicate which type of move
        GlobalArray<unsigned int> m_reject_out_of_cell;       //!< Flags to reject particle moves if they are out of the cell, per particle
        GlobalArray<unsigned int> m_reject;                   //!< Flags to reject particle moves, per particle

        GlobalArray<unsigned int> m_literals;                 //!< Table of literals attached to variables
        GlobalVector<unsigned int> m_n_literals;              //!< Number of literals per variable
        unsigned int m_max_n_literals;                        //!< Max length of a literals (capacity)
        GlobalArray<unsigned int> m_req_n_literals;           //!< Requested number of literals
        GlobalArray<unsigned int> m_req_n_inequality;         //!< Requested number of literals for inequalities

        // inequalities
        GlobalArray<unsigned int> m_inequality_literals;      //!< Table of literals in inequalities, attached to particles
        GlobalVector<unsigned int> m_n_inequality;            //!< Number of inequality literals per varaible
        GlobalArray<double> m_rhs;                            //!< Right hand side of every inequality
        GlobalArray<double> m_coeff;                          //!< Linear coefficient of every literal in an inequality
        unsigned int m_max_n_inequality;                      //!< Max number of literals per particles

        // temporary data structures for SAT solver
        GlobalVector<unsigned int> m_watch;
        GlobalVector<unsigned int> m_state;
        GlobalVector<unsigned int> m_next_clause;
        GlobalVector<unsigned int> m_head;
        GlobalVector<unsigned int> m_next;
        GlobalVector<unsigned int> m_h;

        GlobalVector<unsigned int> m_watch_inequality;
        GlobalVector<unsigned int> m_next_inequality;
        GlobalVector<unsigned int> m_is_watching;
        GlobalVector<unsigned int> m_inequality_begin;
        GlobalVector<double> m_watch_sum;

        GlobalVector<unsigned int> m_component_ptr;
        GlobalVector<unsigned int> m_representative;
        GlobalVector<unsigned int> m_component_size;
        GlobalArray<unsigned int> m_heap;
        GlobalVector<unsigned int> m_colidx_table;
        GlobalVector<unsigned int> m_colidx;

        GlobalVector<unsigned int> m_n_columns;
        GlobalVector<unsigned int> m_csr_row_ptr;
        GlobalArray<unsigned int> m_n_elem;
        GlobalVector<unsigned int> m_work;

        GlobalArray<unsigned int> m_n_depletants;             //!< List of number of depletants, per particle
        GlobalArray<unsigned int> m_n_depletants_ntrial;      //!< List of number of depletants, per particle, trial insertion and configuration:w
        GlobalArray<int> m_deltaF_int;                        //!< Free energy difference delta_F per particle for MH, rescaled to int
        unsigned int m_max_len;                               //!< Max length of shared memory allocation per group
        GlobalArray<unsigned int> m_req_len;                  //!< Requested length of shared mem per group

        detail::UpdateOrderGPU m_update_order;                   //!< Particle update order

        //! For energy evaluation
        GlobalArray<Scalar> m_additive_cutoff;                //!< Per-type additive cutoffs from patch potential

        GlobalArray<hpmc_counters_t> m_counters;                    //!< Per-device counters
        GlobalArray<hpmc_implicit_counters_t> m_implicit_counters;  //!< Per-device counters for depletants

        std::vector<hipStream_t> m_narrow_phase_streams;             //!< Stream for narrow phase kernel, per device
        std::vector<std::vector<hipStream_t> > m_depletant_streams;  //!< Stream for every particle type, and device
        std::vector<std::vector<hipStream_t> > m_depletant_streams_phase1;  //!< Streams for phase1 kernel
        std::vector<std::vector<hipStream_t> > m_depletant_streams_phase2;  //!< Streams for phase2 kernel
        std::vector<std::vector<hipEvent_t> > m_sync;                //!< Synchronization event for every stream and device
        std::vector<std::vector<hipEvent_t> > m_sync_phase1;         //!< Synchronization event for phase1 stream
        std::vector<std::vector<hipEvent_t> > m_sync_phase2;         //!< Synchronization event for phase2 stream

        #ifdef ENABLE_MPI
        std::shared_ptr<MPIConfiguration> m_ntrial_comm;             //!< Communicator for MPI parallel ntrial
        std::shared_ptr<MPIConfiguration> m_particle_comm;           //!< Communicator for MPI particle decomposition
        #endif

        //!< Variables for implicit depletants
        GlobalArray<Scalar> m_lambda;                              //!< Poisson means, per type pair

        //! Set up excell_list
        virtual void initializeExcellMem();

        //! Set the nominal width appropriate for looped moves
        virtual void updateCellWidth();

        //! Update GPU memory hints
        virtual void updateGPUAdvice();

        //! Lazy memory reallocation
        virtual bool checkReallocate();
    };

template< class Shape >
IntegratorHPMCMonoGPU< Shape >::IntegratorHPMCMonoGPU(std::shared_ptr<SystemDefinition> sysdef,
                                                                   std::shared_ptr<CellList> cl,
                                                                   unsigned int seed)
    : IntegratorHPMCMono<Shape>(sysdef, seed), m_cl(cl),
      m_update_order(this->m_exec_conf, seed+this->m_exec_conf->getRank())
    {
    this->m_cl->setRadius(1);
    this->m_cl->setComputeTDB(false);
    this->m_cl->setFlagType();
    this->m_cl->setComputeIdx(true);

    // with multiple GPUs, request a cell list per device
    m_cl->setPerDevice(this->m_exec_conf->allConcurrentManagedAccess());

    // set last dim to a bogus value so that it will re-init on the first call
    m_last_dim = make_uint3(0xffffffff, 0xffffffff, 0xffffffff);
    m_last_nmax = 0xffffffff;

    hipDeviceProp_t dev_prop = this->m_exec_conf->dev_prop;
    m_tuner_moves.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_moves", this->m_exec_conf));
    m_tuner_update_pdata.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_update_pdata", this->m_exec_conf));
    m_tuner_excell_block_size.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_excell_block_size", this->m_exec_conf));

    // tuning parameters for kernels operating on the clauses of the CNF
    std::vector<unsigned int> valid_params_cnf;
    unsigned int warp_size = this->m_exec_conf->dev_prop.warpSize;
    const unsigned int max_literals_block = 64; // a reasonable max
    for (unsigned int block_size = warp_size; block_size <= (unsigned int) dev_prop.maxThreadsPerBlock; block_size += warp_size)
        {
        for (auto l : Autotuner::getTppListPow2(max_literals_block))
            {
            if ((l <= block_size) && ((block_size % l) == 0))
                valid_params_cnf.push_back(block_size*100 + l);
            }
        }
    m_tuner_dependencies.reset(new Autotuner(valid_params_cnf, 5, 100000, "hpmc_dependencies", this->m_exec_conf));
    m_tuner_mem.reset(new Autotuner(valid_params_cnf, 5, 100000, "hpmc_sat_mem", this->m_exec_conf));

    m_tuner_inequalities.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_inequalities", this->m_exec_conf));
    m_tuner_sat.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_sat", this->m_exec_conf));

    m_tuner_num_depletants.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_num_depletants", this->m_exec_conf));
    m_tuner_num_depletants_ntrial.reset(new Autotuner(dev_prop.warpSize, dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_num_depletants_ntrial", this->m_exec_conf));

    // tuning parameters for narrow phase
    std::vector<unsigned int> valid_params;
    const unsigned int narrow_phase_max_tpp = this->m_exec_conf->dev_prop.maxThreadsDim[2];
    for (unsigned int block_size = warp_size; block_size <= (unsigned int) dev_prop.maxThreadsPerBlock; block_size += warp_size)
        {
        for (auto s : Autotuner::getTppListPow2(narrow_phase_max_tpp))
            {
            for (auto t: Autotuner::getTppListPow2(warp_size))
                {
                // only widen the parallelism if the shape supports it
                if (t == 1 || Shape::isParallel())
                    {
                    if ((s*t <= block_size) && ((block_size % (s*t)) == 0))
                        valid_params.push_back(block_size*1000000 + s*100 + t);
                    }
                }
            }
        }

    m_tuner_narrow.reset(new Autotuner(valid_params, 5, 100000, "hpmc_narrow", this->m_exec_conf));

    m_tuner_depletants_accept.reset(new Autotuner(dev_prop.warpSize,
        dev_prop.maxThreadsPerBlock, dev_prop.warpSize, 5, 1000000, "hpmc_depletants_accept", this->m_exec_conf));

    // tuning parameters for depletants
    std::vector<unsigned int> valid_params_depletants;
    for (unsigned int block_size = dev_prop.warpSize; block_size <= (unsigned int) dev_prop.maxThreadsPerBlock; block_size += dev_prop.warpSize)
        {
        for (unsigned int group_size=1; group_size <= narrow_phase_max_tpp; group_size*=2)
            {
            for (unsigned int depletants_per_thread=1; depletants_per_thread <= 32; depletants_per_thread*=2)
                {
                if ((block_size % group_size) == 0)
                    valid_params_depletants.push_back(block_size*1000000 + depletants_per_thread*10000 + group_size);
                }
            }
        }
    m_tuner_depletants.reset(new Autotuner(valid_params_depletants, 5, 100000, "hpmc_depletants", this->m_exec_conf));
    m_tuner_depletants_phase1.reset(new Autotuner(valid_params_depletants, 5, 100000, "hpmc_depletants_phase1", this->m_exec_conf));
    m_tuner_depletants_phase2.reset(new Autotuner(valid_params_depletants, 5, 100000, "hpmc_depletants_phase2", this->m_exec_conf));

    // initialize memory
    GlobalArray<Scalar4>(1,this->m_exec_conf).swap(m_trial_postype);
    TAG_ALLOCATION(m_trial_postype);

    GlobalArray<Scalar4>(1, this->m_exec_conf).swap(m_trial_orientation);
    TAG_ALLOCATION(m_trial_orientation);

    GlobalArray<Scalar4>(1, this->m_exec_conf).swap(m_trial_vel);
    TAG_ALLOCATION(m_trial_vel);

    GlobalArray<unsigned int>(1,this->m_exec_conf).swap(m_trial_move_type);
    TAG_ALLOCATION(m_trial_move_type);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_reject_out_of_cell);
    TAG_ALLOCATION(m_reject_out_of_cell);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_reject);
    TAG_ALLOCATION(m_reject);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_n_literals);
    TAG_ALLOCATION(m_n_literals);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_req_n_literals);
    TAG_ALLOCATION(m_req_n_literals);

        {
        // reset req_n_literals flag
        ArrayHandle<unsigned int> h_req_n_literals(m_req_n_literals, access_location::host, access_mode::overwrite);
        *h_req_n_literals.data = 0;
        }
    m_max_n_literals = 0;

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_n_inequality);
    TAG_ALLOCATION(m_n_inequality);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_req_n_inequality);
    TAG_ALLOCATION(m_req_n_inequality);

        {
        // reset req_n_inequality flag
        ArrayHandle<unsigned int> h_req_n_inequality(m_req_n_inequality, access_location::host, access_mode::overwrite);
        *h_req_n_inequality.data = 0;
        }
    m_max_n_inequality = 0;

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_component_ptr);
    TAG_ALLOCATION(m_component_ptr);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_representative);
    TAG_ALLOCATION(m_representative);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_component_size);
    TAG_ALLOCATION(m_component_size);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_heap);
    TAG_ALLOCATION(m_heap);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_n_columns);
    TAG_ALLOCATION(m_n_columns);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_colidx);
    TAG_ALLOCATION(m_colidx);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_colidx_table);
    TAG_ALLOCATION(m_colidx_table);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_csr_row_ptr);
    TAG_ALLOCATION(m_csr_row_ptr);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_n_elem);
    TAG_ALLOCATION(m_n_elem);

    GlobalVector<unsigned int>(1, this->m_exec_conf).swap(m_work);
    TAG_ALLOCATION(m_work);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_watch);
    TAG_ALLOCATION(m_watch);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_watch_inequality);
    TAG_ALLOCATION(m_watch_inequality);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_state);
    TAG_ALLOCATION(m_state);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_next_clause);
    TAG_ALLOCATION(m_next_clause);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_next_inequality);
    TAG_ALLOCATION(m_next_inequality);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_is_watching);
    TAG_ALLOCATION(m_is_watching);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_inequality_begin);
    TAG_ALLOCATION(m_inequality_begin);

    GlobalVector<double>(this->m_exec_conf).swap(m_watch_sum);
    TAG_ALLOCATION(m_watch_sum);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_head);
    TAG_ALLOCATION(m_head);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_next);
    TAG_ALLOCATION(m_next);

    GlobalVector<unsigned int>(this->m_exec_conf).swap(m_h);
    TAG_ALLOCATION(m_h);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_req_len);
    TAG_ALLOCATION(m_req_len);

    m_max_len = 0;

        {
        // reset req_len flag for depletants
        ArrayHandle<unsigned int> h_req_len(m_req_len, access_location::host, access_mode::overwrite);
        *h_req_len.data = 0;
        }

    GlobalArray<unsigned int> excell_size(0, this->m_exec_conf);
    m_excell_size.swap(excell_size);
    TAG_ALLOCATION(m_excell_size);

    GlobalArray<unsigned int> excell_idx(0, this->m_exec_conf);
    m_excell_idx.swap(excell_idx);
    TAG_ALLOCATION(m_excell_idx);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_n_depletants);
    TAG_ALLOCATION(m_n_depletants);

    GlobalArray<unsigned int>(1, this->m_exec_conf).swap(m_n_depletants_ntrial);
    TAG_ALLOCATION(m_n_depletants_ntrial);

    GlobalArray<int>(1, this->m_exec_conf).swap(m_deltaF_int);
    TAG_ALLOCATION(m_deltaF_int);

    //! One counter per GPU, separated by an entire memory page
    unsigned int pitch = (getpagesize() + sizeof(hpmc_counters_t)-1)/sizeof(hpmc_counters_t);
    GlobalArray<hpmc_counters_t>(pitch, this->m_exec_conf->getNumActiveGPUs(), this->m_exec_conf).swap(m_counters);
    TAG_ALLOCATION(m_counters);

    #ifdef __HIP_PLATFORM_NVCC__
    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // set memory hints
        auto gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            cudaMemAdvise(m_counters.get()+idev*m_counters.getPitch(), sizeof(hpmc_counters_t)*m_counters.getPitch(), cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_counters.get()+idev*m_counters.getPitch(), sizeof(hpmc_counters_t)*m_counters.getPitch(), gpu_map[idev]);
            }
        CHECK_CUDA_ERROR();
        }
    #endif

    // ntypes counters per GPU, separated by at least a memory page
    pitch = (getpagesize() + sizeof(hpmc_implicit_counters_t)-1)/sizeof(hpmc_implicit_counters_t);
    GlobalArray<hpmc_implicit_counters_t>(std::max(pitch, this->m_implicit_count.getNumElements()),
        this->m_exec_conf->getNumActiveGPUs(), this->m_exec_conf).swap(m_implicit_counters);
    TAG_ALLOCATION(m_implicit_counters);

    #ifdef __HIP_PLATFORM_NVCC__
    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // set memory hints
        auto gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            cudaMemAdvise(m_implicit_counters.get()+idev*m_implicit_counters.getPitch(),
                sizeof(hpmc_implicit_counters_t)*m_implicit_counters.getPitch(), cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_implicit_counters.get()+idev*m_implicit_counters.getPitch(),
                sizeof(hpmc_implicit_counters_t)*m_implicit_counters.getPitch(), gpu_map[idev]);
            }
        }
    #endif

    m_narrow_phase_streams.resize(this->m_exec_conf->getNumActiveGPUs());
    for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
        {
        hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
        hipStreamCreate(&m_narrow_phase_streams[idev]);
        }

    // Depletants
    unsigned int ntypes = this->m_pdata->getNTypes();
    GlobalArray<Scalar> lambda(ntypes*this->m_depletant_idx.getNumElements(), this->m_exec_conf);
    m_lambda.swap(lambda);
    TAG_ALLOCATION(m_lambda);

    m_depletant_streams.resize(this->m_depletant_idx.getNumElements());
    m_depletant_streams_phase1.resize(this->m_depletant_idx.getNumElements());
    m_depletant_streams_phase2.resize(this->m_depletant_idx.getNumElements());
    for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
        {
        for (unsigned int jtype = 0; jtype < this->m_pdata->getNTypes(); ++jtype)
            {
            m_depletant_streams[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            m_depletant_streams_phase1[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            m_depletant_streams_phase2[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                {
                hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
                hipStreamCreate(&m_depletant_streams[this->m_depletant_idx(itype,jtype)][idev]);
                hipStreamCreate(&m_depletant_streams_phase1[this->m_depletant_idx(itype,jtype)][idev]);
                hipStreamCreate(&m_depletant_streams_phase2[this->m_depletant_idx(itype,jtype)][idev]);
                }
            }
        }

    // synchronization events
    m_sync.resize(this->m_depletant_idx.getNumElements());
    m_sync_phase1.resize(this->m_depletant_idx.getNumElements());
    m_sync_phase2.resize(this->m_depletant_idx.getNumElements());
    for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
        {
        for (unsigned int jtype = 0; jtype < this->m_pdata->getNTypes(); ++jtype)
            {
            m_sync[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            m_sync_phase1[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            m_sync_phase2[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
            for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                {
                hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
                hipEventCreateWithFlags(&m_sync[this->m_depletant_idx(itype,jtype)][idev],hipEventDisableTiming);
                hipEventCreateWithFlags(&m_sync_phase1[this->m_depletant_idx(itype,jtype)][idev],hipEventDisableTiming);
                hipEventCreateWithFlags(&m_sync_phase2[this->m_depletant_idx(itype,jtype)][idev],hipEventDisableTiming);
                }
            }
        }

    #ifdef __HIP_PLATFORM_NVCC__
    // memory hint for overlap matrix
    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        cudaMemAdvise(this->m_overlaps.get(), sizeof(unsigned int)*this->m_overlaps.getNumElements(), cudaMemAdviseSetReadMostly, 0);
        CHECK_CUDA_ERROR();
        }
    #endif

    // patch
    GlobalArray<Scalar>(this->m_pdata->getNTypes(), this->m_exec_conf).swap(m_additive_cutoff);
    TAG_ALLOCATION(m_additive_cutoff);
    }

template< class Shape >
IntegratorHPMCMonoGPU< Shape >::~IntegratorHPMCMonoGPU()
    {
    // release resources
    for (auto s: m_depletant_streams)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipStreamDestroy(s[idev]);
            }
        }

    for (auto s: m_depletant_streams_phase1)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipStreamDestroy(s[idev]);
            }
        }

    for (auto s: m_depletant_streams_phase2)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipStreamDestroy(s[idev]);
            }
        }

   for (auto s: m_sync)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipEventDestroy(s[idev]);
            }
        }

    for (auto s: m_sync_phase1)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipEventDestroy(s[idev]);
            }
        }

    for (auto s: m_sync_phase2)
        {
        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
            {
            hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
            hipEventDestroy(s[idev]);
            }
        }

    for (int idev = this->m_exec_conf->getNumActiveGPUs() -1; idev >= 0; --idev)
        {
        hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
        hipStreamDestroy(m_narrow_phase_streams[idev]);
        }
    }

template< class Shape >
void IntegratorHPMCMonoGPU< Shape >::updateGPUAdvice()
    {
    #ifdef __HIP_PLATFORM_NVCC__
    // update memory hints
    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // set memory hints
        auto gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            auto range = this->m_pdata->getGPUPartition().getRange(idev);

            unsigned int nelem = range.second-range.first;
            if (nelem == 0)
                continue;

            cudaMemAdvise(m_trial_postype.get()+range.first, sizeof(Scalar4)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_trial_postype.get()+range.first, sizeof(Scalar4)*nelem, gpu_map[idev]);

            cudaMemAdvise(m_trial_move_type.get()+range.first, sizeof(unsigned int)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_trial_move_type.get()+range.first, sizeof(unsigned int)*nelem, gpu_map[idev]);

            cudaMemAdvise(m_reject.get()+range.first, sizeof(unsigned int)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_reject.get()+range.first, sizeof(unsigned int)*nelem, gpu_map[idev]);

            cudaMemAdvise(m_trial_orientation.get()+range.first, sizeof(Scalar4)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_trial_orientation.get()+range.first, sizeof(Scalar4)*nelem, gpu_map[idev]);

            cudaMemAdvise(m_trial_vel.get()+range.first, sizeof(Scalar4)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_trial_vel.get()+range.first, sizeof(Scalar4)*nelem, gpu_map[idev]);

            cudaMemAdvise(m_reject_out_of_cell.get()+range.first, sizeof(unsigned int)*nelem, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
            cudaMemPrefetchAsync(m_reject_out_of_cell.get()+range.first, sizeof(unsigned int)*nelem, gpu_map[idev]);

            unsigned int ntrial_offset = 0;
            for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
                {
                // need to iterate in the same itype <= jtype order as in the update loop where this array is consumed
                for (unsigned int jtype = itype; jtype < this->m_pdata->getNTypes(); ++jtype)
                    {
                    if (this->m_fugacity[this->m_depletant_idx(itype,jtype)] == 0)
                        continue;

                    cudaMemAdvise(m_n_depletants.get()+this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN()+range.first,
                        sizeof(unsigned int)*nelem,
                        cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
                    cudaMemPrefetchAsync(m_n_depletants.get()+this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN()+range.first,
                        sizeof(unsigned int)*nelem, gpu_map[idev]);

                    unsigned int ntrial = this->m_ntrial[this->m_depletant_idx(itype,jtype)];
                    if (ntrial == 0)
                        continue;

                    cudaMemAdvise(m_n_depletants_ntrial.get() + ntrial_offset + range.first,
                        sizeof(unsigned int)*nelem*2*ntrial, cudaMemAdviseSetPreferredLocation, gpu_map[idev]);
                    cudaMemPrefetchAsync(m_n_depletants_ntrial.get() + ntrial_offset + range.first,
                        sizeof(unsigned int)*nelem*2*ntrial, gpu_map[idev]);
                    ntrial_offset += ntrial*2*this->m_pdata->getMaxN();

                    cudaMemAdvise(m_deltaF_int.get()+
                        this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN()+
                        range.first,
                        sizeof(int)*nelem,
                        cudaMemAdviseSetPreferredLocation,
                        gpu_map[idev]);
                    cudaMemPrefetchAsync(m_deltaF_int.get()+
                        this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN()+
                        range.first,
                        sizeof(int)*nelem,
                        gpu_map[idev]);
                    CHECK_CUDA_ERROR();
                    }
                }

            }
        }
    #endif
    }

template< class Shape >
void IntegratorHPMCMonoGPU< Shape >::update(unsigned int timestep)
    {
    IntegratorHPMC::update(timestep);

    if (this->m_patch && !this->m_patch_log)
        {
        ArrayHandle<Scalar> h_additive_cutoff(m_additive_cutoff, access_location::host, access_mode::overwrite);
        for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
            {
            h_additive_cutoff.data[itype] = this->m_patch->getAdditiveCutoff(itype);
            }
        }

    // rng for shuffle and grid shift
    hoomd::RandomGenerator rng(hoomd::RNGIdentifier::HPMCMonoShift, this->m_seed, timestep);

    if (this->m_pdata->getN() > 0)
        {
        // compute the width of the active region
        Scalar3 npd = this->m_pdata->getBox().getNearestPlaneDistance();
        Scalar3 ghost_fraction = this->m_nominal_width / npd;

        // check if we are below a minimum image convention box size
        // the minimum image convention comes from the global box, not the local one
        BoxDim global_box = this->m_pdata->getGlobalBox();
        Scalar3 nearest_plane_distance = global_box.getNearestPlaneDistance();

        if ((global_box.getPeriodic().x && nearest_plane_distance.x <= this->m_nominal_width*2) ||
            (global_box.getPeriodic().y && nearest_plane_distance.y <= this->m_nominal_width*2) ||
            (this->m_sysdef->getNDimensions() == 3 && global_box.getPeriodic().z && nearest_plane_distance.z <= this->m_nominal_width*2))
            {
            this->m_exec_conf->msg->error() << "Simulation box too small for GPU accelerated HPMC execution - increase it so the minimum image convention works" << std::endl;
            throw std::runtime_error("Error performing HPMC update");
            }

        // update the cell list
        this->m_cl->compute(timestep);

        // start the profile
        if (this->m_prof) this->m_prof->push(this->m_exec_conf, "HPMC");

        // if the cell list is a different size than last time, reinitialize the expanded cell list
        uint3 cur_dim = this->m_cl->getDim();
        if (m_last_dim.x != cur_dim.x || m_last_dim.y != cur_dim.y || m_last_dim.z != cur_dim.z
            || m_last_nmax != this->m_cl->getNmax())
            {
            initializeExcellMem();

            m_last_dim = cur_dim;
            m_last_nmax = this->m_cl->getNmax();
            }

        // test if we are in domain decomposition mode
        bool domain_decomposition = false;
#ifdef ENABLE_MPI
        if (this->m_comm)
            domain_decomposition = true;
#endif

        // resize some arrays
        bool resized = m_reject.getNumElements() < this->m_pdata->getMaxN();

        bool update_gpu_advice = false;

        if (resized)
            {
            m_reject.resize(this->m_pdata->getMaxN());
            m_reject_out_of_cell.resize(this->m_pdata->getMaxN());
            m_trial_postype.resize(this->m_pdata->getMaxN());
            m_trial_orientation.resize(this->m_pdata->getMaxN());
            m_trial_vel.resize(this->m_pdata->getMaxN());
            m_trial_move_type.resize(this->m_pdata->getMaxN());
            m_n_literals.resize(this->m_pdata->getMaxN());
            m_n_inequality.resize(this->m_pdata->getMaxN());

            update_gpu_advice = true;
            }

        if (m_n_depletants.getNumElements() < this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements())
            {
            m_n_depletants.resize(this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements());
            update_gpu_advice = true;
            }

        // resize data structures for depletants with ntrial > 0
        bool have_auxilliary_variables = false;
        bool have_depletants = false;
        unsigned int ntrial_tot = 0;

        #ifdef ENABLE_MPI
        int ntrial_comm_size;
        int ntrial_comm_rank;
        if (m_ntrial_comm)
            {
            MPI_Comm_size((*m_ntrial_comm)(), &ntrial_comm_size);
            MPI_Comm_rank((*m_ntrial_comm)(), &ntrial_comm_rank);
            }
        #endif

        GPUPartition gpu_partition_rank = this->m_pdata->getGPUPartition();
        unsigned int nparticles_rank = this->m_pdata->getN();

        #ifdef ENABLE_MPI
        int particle_comm_size;
        int particle_comm_rank;
        if (m_particle_comm)
            {
            // split local particle data further if a communicator is supplied
            MPI_Comm_size((*m_particle_comm)(), &particle_comm_size);
            MPI_Comm_rank((*m_particle_comm)(), &particle_comm_rank);

            nparticles_rank = this->m_pdata->getN()/particle_comm_size + 1;
            unsigned int offset = (particle_comm_rank*nparticles_rank < this->m_pdata->getN()) ?
                particle_comm_rank*nparticles_rank : this->m_pdata->getN();
            unsigned np = ((offset + nparticles_rank) < this->m_pdata->getN()) ?
                nparticles_rank : (this->m_pdata->getN() - offset);
            gpu_partition_rank.setN(np, offset);
            }
        #endif

        for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
            {
            for (unsigned int jtype = itype; jtype < this->m_pdata->getNTypes(); ++jtype)
                {
                if (this->m_fugacity[this->m_depletant_idx(itype,jtype)] == 0)
                    continue;
                have_depletants = true;
                unsigned int ntrial = this->m_ntrial[this->m_depletant_idx(itype,jtype)];
                if (ntrial == 0)
                    continue;
                have_auxilliary_variables = true;
                ntrial_tot += ntrial;
                }
            }
        unsigned int req_n_depletants_size = ntrial_tot*2*this->m_pdata->getMaxN();
        if (req_n_depletants_size > m_n_depletants_ntrial.getNumElements())
            {
            m_n_depletants_ntrial.resize(req_n_depletants_size);
            update_gpu_advice = true;
            }

        if (have_depletants && have_auxilliary_variables &&
            m_deltaF_int.getNumElements() <
              this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements())
            {
            m_deltaF_int.resize(this->m_pdata->getMaxN()*
                this->m_depletant_idx.getNumElements());
            update_gpu_advice = true;
            }

        if (update_gpu_advice)
            updateGPUAdvice();

        m_update_order.resize(this->m_pdata->getN());

        // access the cell list data
        ArrayHandle<unsigned int> d_cell_size(this->m_cl->getCellSizeArray(), access_location::device, access_mode::read);
        ArrayHandle<unsigned int> d_cell_idx(this->m_cl->getIndexArray(), access_location::device, access_mode::read);
        ArrayHandle<unsigned int> d_cell_adj(this->m_cl->getCellAdjArray(), access_location::device, access_mode::read);

        // per-device cell list data
        const ArrayHandle<unsigned int>& d_cell_size_per_device = m_cl->getPerDevice() ?
            ArrayHandle<unsigned int>(m_cl->getCellSizeArrayPerDevice(),access_location::device, access_mode::read) :
            ArrayHandle<unsigned int>(GlobalArray<unsigned int>(), access_location::device, access_mode::read);
        const ArrayHandle<unsigned int>& d_cell_idx_per_device = m_cl->getPerDevice() ?
            ArrayHandle<unsigned int>(m_cl->getIndexArrayPerDevice(), access_location::device, access_mode::read) :
            ArrayHandle<unsigned int>(GlobalArray<unsigned int>(), access_location::device, access_mode::read);

        unsigned int ngpu = this->m_exec_conf->getNumActiveGPUs();
        if (ngpu > 1)
            {
            // reset per-device counters
            ArrayHandle<hpmc_counters_t> d_counters_per_device(this->m_counters, access_location::device, access_mode::overwrite);
            hipMemset(d_counters_per_device.data, 0, sizeof(hpmc_counters_t)*this->m_counters.getNumElements());
            if (this->m_exec_conf->isCUDAErrorCheckingEnabled()) CHECK_CUDA_ERROR();

            ArrayHandle<hpmc_implicit_counters_t> d_implicit_counters_per_device(this->m_implicit_counters, access_location::device, access_mode::overwrite);
            hipMemset(d_implicit_counters_per_device.data, 0, sizeof(hpmc_implicit_counters_t)*this->m_implicit_counters.getNumElements());
            if (this->m_exec_conf->isCUDAErrorCheckingEnabled()) CHECK_CUDA_ERROR();
            }

        // access the parameters and interaction matrix
        auto & params = this->getParams();

        ArrayHandle<unsigned int> d_overlaps(this->m_overlaps, access_location::device, access_mode::read);

        // access the move sizes by type
        ArrayHandle<Scalar> d_d(this->m_d, access_location::device, access_mode::read);
        ArrayHandle<Scalar> d_a(this->m_a, access_location::device, access_mode::read);

        BoxDim box = this->m_pdata->getBox();

        Scalar3 ghost_width = this->m_cl->getGhostWidth();

        // randomize particle update order
        this->m_update_order.shuffle(timestep);

        // expanded cells & neighbor list
        ArrayHandle< unsigned int > d_excell_idx(m_excell_idx, access_location::device, access_mode::overwrite);
        ArrayHandle< unsigned int > d_excell_size(m_excell_size, access_location::device, access_mode::overwrite);

        // update the expanded cells
        this->m_tuner_excell_block_size->begin();
        gpu::hpmc_excell(d_excell_idx.data,
                            d_excell_size.data,
                            m_excell_list_indexer,
                            m_cl->getPerDevice() ? d_cell_idx_per_device.data : d_cell_idx.data,
                            m_cl->getPerDevice() ? d_cell_size_per_device.data : d_cell_size.data,
                            d_cell_adj.data,
                            this->m_cl->getCellIndexer(),
                            this->m_cl->getCellListIndexer(),
                            this->m_cl->getCellAdjIndexer(),
                            this->m_exec_conf->getNumActiveGPUs(),
                            this->m_tuner_excell_block_size->getParam());
        if (this->m_exec_conf->isCUDAErrorCheckingEnabled()) CHECK_CUDA_ERROR();
        this->m_tuner_excell_block_size->end();

        // depletants
        ArrayHandle<Scalar> d_lambda(m_lambda, access_location::device, access_mode::read);

        for (unsigned int i = 0; i < this->m_nselect; i++)
            {
            if (this->m_prof) this->m_prof->push(this->m_exec_conf, "Propose moves");

                { // ArrayHandle scope
                ArrayHandle<unsigned int> d_update_order_by_ptl(m_update_order.get(), access_location::device, access_mode::read);
                ArrayHandle<unsigned int> d_reject_out_of_cell(m_reject_out_of_cell, access_location::device, access_mode::overwrite);
                ArrayHandle<unsigned int> d_reject(m_reject, access_location::device, access_mode::overwrite);

                // access data for proposed moves
                ArrayHandle<Scalar4> d_trial_postype(m_trial_postype, access_location::device, access_mode::overwrite);
                ArrayHandle<Scalar4> d_trial_orientation(m_trial_orientation, access_location::device, access_mode::overwrite);
                ArrayHandle<Scalar4> d_trial_vel(m_trial_vel, access_location::device, access_mode::overwrite);
                ArrayHandle<unsigned int> d_trial_move_type(m_trial_move_type, access_location::device, access_mode::overwrite);

                // access the particle data
                ArrayHandle<Scalar4> d_postype(this->m_pdata->getPositions(), access_location::device, access_mode::read);
                ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::read);
                ArrayHandle<Scalar4> d_vel(this->m_pdata->getVelocities(), access_location::device, access_mode::read);

                // MC counters
                ArrayHandle<hpmc_counters_t> d_counters(this->m_count_total, access_location::device, access_mode::read);
                ArrayHandle<hpmc_counters_t> d_counters_per_device(this->m_counters, access_location::device, access_mode::read);

                // fill the parameter structure for the GPU kernels
                gpu::hpmc_args_t args(
                    d_postype.data,
                    d_orientation.data,
                    d_vel.data,
                    ngpu > 1 ? d_counters_per_device.data : d_counters.data,
                    this->m_counters.getPitch(),
                    this->m_cl->getCellIndexer(),
                    this->m_cl->getDim(),
                    ghost_width,
                    this->m_pdata->getN(),
                    this->m_pdata->getNTypes(),
                    this->m_seed + this->m_exec_conf->getRank()*this->m_nselect + i,
                    d_d.data,
                    d_a.data,
                    d_overlaps.data,
                    this->m_overlap_idx,
                    this->m_move_ratio,
                    timestep,
                    this->m_sysdef->getNDimensions(),
                    box,
                    i,
                    ghost_fraction,
                    domain_decomposition,
                    0, // block size
                    0, // tpp
                    0, // overlap_threads
                    have_auxilliary_variables,
                    d_reject_out_of_cell.data,
                    d_trial_postype.data,
                    d_trial_orientation.data,
                    d_trial_vel.data,
                    d_trial_move_type.data,
                    d_update_order_by_ptl.data,
                    d_excell_idx.data,
                    d_excell_size.data,
                    m_excell_list_indexer,
                    this->m_exec_conf->dev_prop,
                    this->m_pdata->getGPUPartition(),
                    0,// streams,
                    0,// d_literals
                    0,// d_n_literals
                    0,// max_n_literals
                    0 // d_req_n_literals
                    );

                // propose trial moves, \sa gpu::kernel::hpmc_moves

                // reset acceptance results and move types
                m_tuner_moves->begin();
                args.block_size = m_tuner_moves->getParam();
                gpu::hpmc_gen_moves<Shape>(args, params.data());
                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                    CHECK_CUDA_ERROR();
                m_tuner_moves->end();
                }

            if (this->m_prof) this->m_prof->pop(this->m_exec_conf);

            do
                {
                // reset free energy accumulators
                ArrayHandle<int> d_deltaF_int(m_deltaF_int, access_location::device, access_mode::readwrite);
                ArrayHandle<Scalar> h_fugacity(this->m_fugacity, access_location::host, access_mode::read);
                ArrayHandle<unsigned int> h_ntrial(this->m_ntrial, access_location::host, access_mode::read);
                ArrayHandle<unsigned> d_req_len(m_req_len, access_location::device, access_mode::readwrite);

                this->m_exec_conf->beginMultiGPU();
                for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
                    {
                    for (unsigned int jtype = itype; jtype < this->m_pdata->getNTypes(); ++jtype)
                        {
                        if (h_fugacity.data[this->m_depletant_idx(itype,jtype)] == 0)
                            continue;

                        unsigned int ntrial = h_ntrial.data[this->m_depletant_idx(itype,jtype)];
                        if (ntrial)
                            {
                            for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                                {
                                hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);

                                auto range = this->m_pdata->getGPUPartition().getRange(idev);
                                if (range.second - range.first != 0)
                                    {
                                    hipMemsetAsync(d_deltaF_int.data +
                                        this->m_pdata->getMaxN()*this->m_depletant_idx(itype,jtype) +
                                        range.first,
                                        0,
                                        sizeof(int)*(range.second-range.first),
                                        m_depletant_streams[this->m_depletant_idx(itype,jtype)][idev]);
                                    }
                                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                    CHECK_CUDA_ERROR();
                                }
                            }
                        }
                    }
                this->m_exec_conf->endMultiGPU();

                    {
                    ArrayHandle<unsigned int> d_reject(this->m_reject, access_location::device, access_mode::overwrite);
                    ArrayHandle<unsigned int> d_literals(this->m_literals, access_location::device, access_mode::overwrite);
                    ArrayHandle<unsigned int> d_n_literals(this->m_n_literals, access_location::device, access_mode::overwrite);
                    ArrayHandle<unsigned int> d_req_n_literals(this->m_req_n_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_n_inequality(this->m_n_inequality, access_location::device, access_mode::overwrite);

                    // reset mem
                    hipMemsetAsync(d_reject.data, 0, sizeof(unsigned int)*m_reject.getNumElements());
                    hipMemsetAsync(d_n_literals.data, 0, sizeof(unsigned int)*m_n_literals.getNumElements());
                    hipMemsetAsync(d_n_inequality.data, 0, sizeof(unsigned int)*m_n_inequality.getNumElements());
                    if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                        CHECK_CUDA_ERROR();
                    }


                if (this->m_prof) this->m_prof->push(this->m_exec_conf, "Check overlaps");

                    {
                    // ArrayHandle scope
                    ArrayHandle<unsigned int> d_update_order_by_ptl(m_update_order.get(), access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_reject(m_reject, access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_reject_out_of_cell(m_reject_out_of_cell, access_location::device, access_mode::read);

                    // access data for proposed moves
                    ArrayHandle<Scalar4> d_trial_postype(m_trial_postype, access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_trial_orientation(m_trial_orientation, access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_trial_vel(m_trial_vel, access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_trial_move_type(m_trial_move_type, access_location::device, access_mode::read);

                    // access the particle data
                    ArrayHandle<Scalar4> d_postype(this->m_pdata->getPositions(), access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_vel(this->m_pdata->getVelocities(), access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_tag(this->m_pdata->getTags(), access_location::device, access_mode::read);

                    // MC counters
                    ArrayHandle<hpmc_counters_t> d_counters(this->m_count_total, access_location::device, access_mode::readwrite);
                    ArrayHandle<hpmc_counters_t> d_counters_per_device(this->m_counters, access_location::device, access_mode::readwrite);

                    // CNF
                    ArrayHandle<unsigned int> d_literals(this->m_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_n_literals(this->m_n_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_req_n_literals(this->m_req_n_literals, access_location::device, access_mode::readwrite);

                    // inequalities
                    ArrayHandle<unsigned int> d_n_inequality(this->m_n_inequality, access_location::device, access_mode::readwrite);

                    // depletant counters
                    ArrayHandle<hpmc_implicit_counters_t> d_implicit_count(this->m_implicit_count, access_location::device, access_mode::readwrite);
                    ArrayHandle<hpmc_implicit_counters_t> d_implicit_counters_per_device(this->m_implicit_counters, access_location::device, access_mode::readwrite);

                    // depletants
                    ArrayHandle<unsigned int> d_n_depletants(m_n_depletants, access_location::device, access_mode::overwrite);
                    ArrayHandle<unsigned int> d_n_depletants_ntrial(m_n_depletants_ntrial, access_location::device, access_mode::overwrite);

                    // fill the parameter structure for the GPU kernels
                    gpu::hpmc_args_t args(
                        d_postype.data,
                        d_orientation.data,
                        d_vel.data,
                        ngpu > 1 ? d_counters_per_device.data : d_counters.data,
                        this->m_counters.getPitch(),
                        this->m_cl->getCellIndexer(),
                        this->m_cl->getDim(),
                        ghost_width,
                        this->m_pdata->getN(),
                        this->m_pdata->getNTypes(),
                        this->m_seed,
                        d_d.data,
                        d_a.data,
                        d_overlaps.data,
                        this->m_overlap_idx,
                        this->m_move_ratio,
                        timestep,
                        this->m_sysdef->getNDimensions(),
                        box,
                        this->m_exec_conf->getRank()*this->m_nselect + i,
                        ghost_fraction,
                        domain_decomposition,
                        0, // block size
                        0, // tpp
                        0, // overlap threads
                        have_auxilliary_variables,
                        d_reject_out_of_cell.data,
                        d_trial_postype.data,
                        d_trial_orientation.data,
                        d_trial_vel.data,
                        d_trial_move_type.data,
                        d_update_order_by_ptl.data,
                        d_excell_idx.data,
                        d_excell_size.data,
                        m_excell_list_indexer,
                        this->m_exec_conf->dev_prop,
                        this->m_pdata->getGPUPartition(),
                        &m_narrow_phase_streams.front(),
                        d_literals.data,
                        d_n_literals.data,
                        m_max_n_literals,
                        d_req_n_literals.data);

                    /*
                     *  check overlaps, new configuration simultaneously against the old and the new configuration
                     */

                    this->m_exec_conf->beginMultiGPU();
                    m_tuner_narrow->begin();
                    unsigned int param = m_tuner_narrow->getParam();
                    args.block_size = param/1000000;
                    args.tpp = (param%1000000)/100;
                    args.overlap_threads = param%100;
                    gpu::hpmc_narrow_phase<Shape>(args, params.data());
                    if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                        CHECK_CUDA_ERROR();
                    m_tuner_narrow->end();
                    this->m_exec_conf->endMultiGPU();
                    }

                if (this->m_prof) this->m_prof->pop(this->m_exec_conf);

                #if 0
                /*
                 * Insert depletants
                 */

                this->m_exec_conf->beginMultiGPU();

                unsigned int ntrial_offset = 0;

                // allow concurrency between depletant types in multi GPU block
                for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
                    {
                    for (unsigned int jtype = itype; jtype < this->m_pdata->getNTypes(); ++jtype)
                        {
                        if (h_fugacity.data[this->m_depletant_idx(itype,jtype)] == 0)
                            continue;

                        unsigned int ntrial = h_ntrial.data[this->m_depletant_idx(itype,jtype)];
                        if (!ntrial)
                            {
                            // draw random number of depletant insertions per particle from Poisson distribution
                            m_tuner_num_depletants->begin();
                            gpu::generate_num_depletants(
                                this->m_seed,
                                timestep,
                                this->m_exec_conf->getRank()*this->m_nselect + i,
                                itype,
                                jtype,
                                this->m_depletant_idx,
                                d_lambda.data,
                                d_postype.data,
                                d_n_depletants.data + this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN(),
                                m_tuner_num_depletants->getParam(),
                                &m_depletant_streams[this->m_depletant_idx(itype,jtype)].front(),
                                this->m_pdata->getGPUPartition());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();
                            m_tuner_num_depletants->end();

                            // max reduce over result
                            unsigned int max_n_depletants[this->m_exec_conf->getNumActiveGPUs()];
                            gpu::get_max_num_depletants(
                                d_n_depletants.data + this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN(),
                                &max_n_depletants[0],
                                &m_depletant_streams[this->m_depletant_idx(itype,jtype)].front(),
                                this->m_pdata->getGPUPartition(),
                                this->m_exec_conf->getCachedAllocatorManaged());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();

                            // insert depletants on-the-fly
                            m_tuner_depletants->begin();
                            unsigned int param = m_tuner_depletants->getParam();
                            args.block_size = param/1000000;
                            unsigned int depletants_per_thread = (param % 1000000)/10000;
                            args.tpp = param%10000;

                            gpu::hpmc_implicit_args_t implicit_args(
                                itype,
                                jtype,
                                this->m_depletant_idx,
                                ngpu > 1 ? d_implicit_counters_per_device.data : d_implicit_count.data,
                                m_implicit_counters.getPitch(),
                                h_fugacity.data[this->m_depletant_idx(itype,jtype)] < 0,
                                d_n_depletants.data + this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN(),
                                &max_n_depletants[0],
                                depletants_per_thread,
                                &m_depletant_streams[this->m_depletant_idx(itype,jtype)].front()
                                );
                            gpu::hpmc_insert_depletants<Shape>(args, implicit_args, params.data());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();
                            m_tuner_depletants->end();
                            }
                        else
                            {
                            // generate random number of depletant insertions per particle, trial insertion and configuration
                            m_tuner_num_depletants_ntrial->begin();
                            gpu::generate_num_depletants_ntrial(
                                d_vel.data,
                                d_trial_vel.data,
                                ntrial,
                                itype,
                                jtype,
                                this->m_depletant_idx,
                                d_lambda.data,
                                d_postype.data,
                                d_n_depletants_ntrial.data + ntrial_offset,
                                this->m_pdata->getN(),
                                particle_comm_rank == particle_comm_size - 1,
                                this->m_pdata->getNGhosts(),
                                gpu_partition_rank,
                                m_tuner_num_depletants_ntrial->getParam(),
                                &m_depletant_streams[this->m_depletant_idx(itype,jtype)].front());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();
                            m_tuner_num_depletants_ntrial->end();

                            // max reduce over result
                            unsigned int max_n_depletants[this->m_exec_conf->getNumActiveGPUs()];
                            gpu::get_max_num_depletants_ntrial(
                                ntrial,
                                d_n_depletants_ntrial.data + ntrial_offset,
                                &max_n_depletants[0],
                                particle_comm_rank == particle_comm_size - 1,
                                this->m_pdata->getNGhosts(),
                                &m_depletant_streams[this->m_depletant_idx(itype,jtype)].front(),
                                gpu_partition_rank,
                                this->m_exec_conf->getCachedAllocatorManaged());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();

                            // the next two kernels can be launched concurrently on different streams
                            // sync those streams with the parent stream
                            auto gpu_map = this->m_exec_conf->getGPUIds();

                            for (int idev = gpu_map.size() - 1; idev >= 0; --idev)
                                {
                                hipStream_t parent = m_depletant_streams[this->m_depletant_idx(itype,jtype)][idev];
                                hipStream_t s1 = m_depletant_streams_phase1[this->m_depletant_idx(itype,jtype)][idev];
                                hipStream_t s2 = m_depletant_streams_phase2[this->m_depletant_idx(itype,jtype)][idev];
                                hipSetDevice(gpu_map[idev]);
                                hipEventRecord(m_sync[this->m_depletant_idx(itype,jtype)][idev], parent);
                                hipStreamWaitEvent(s1, m_sync[this->m_depletant_idx(itype,jtype)][idev], 0);
                                hipStreamWaitEvent(s2, m_sync[this->m_depletant_idx(itype,jtype)][idev], 0);
                                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                    CHECK_CUDA_ERROR();
                                }

                            // insert depletants
                            gpu::hpmc_implicit_args_t implicit_args(
                                itype,
                                jtype,
                                this->m_depletant_idx,
                                ngpu > 1 ? d_implicit_counters_per_device.data : d_implicit_count.data,
                                m_implicit_counters.getPitch(),
                                h_fugacity.data[this->m_depletant_idx(itype,jtype)] < 0,
                                0, // d_n_depletants (unused)
                                &max_n_depletants[0],
                                0,// depletants_per_thread
                                0 // stream (unused)
                                );

                            unsigned int nwork_rank[this->m_exec_conf->getNumActiveGPUs()];
                            unsigned int work_offset[this->m_exec_conf->getNumActiveGPUs()];
                            for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
                                {
                                nwork_rank[idev] = ntrial*max_n_depletants[idev];
                                work_offset[idev] = 0;
                                }

                            #ifdef ENABLE_MPI
                            if (m_ntrial_comm)
                                {
                                // split up work among ranks
                                for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
                                    {
                                    nwork_rank[idev] = nwork_rank[idev]/ntrial_comm_size + 1; // can't have zero work per rank
                                    work_offset[idev] = ntrial_comm_rank*nwork_rank[idev];
                                    }
                                }
                            #endif

                            gpu::hpmc_auxilliary_args_t auxilliary_args(
                                d_tag.data,
                                d_vel.data,
                                d_trial_vel.data,
                                ntrial,
                                &nwork_rank[0],
                                &work_offset[0],
                                d_n_depletants_ntrial.data + ntrial_offset,
                                d_deltaF_int.data + this->m_depletant_idx(itype,jtype)*this->m_pdata->getMaxN(),
                                &m_depletant_streams_phase1[this->m_depletant_idx(itype,jtype)].front(),
                                &m_depletant_streams_phase2[this->m_depletant_idx(itype,jtype)].front(),
                                m_max_len,
                                d_req_len.data,
                                particle_comm_rank == particle_comm_size - 1,
                                this->m_pdata->getNGhosts(),
                                gpu_partition_rank);

                            // phase 1, insert into excluded volume of particle i
                            m_tuner_depletants_phase1->begin();
                            unsigned int param = m_tuner_depletants_phase1->getParam();
                            args.block_size = param/1000000;
                            implicit_args.depletants_per_thread = (param % 1000000)/10000;
                            args.tpp = param%10000;
                            gpu::hpmc_depletants_auxilliary_phase1<Shape>(args,
                                implicit_args,
                                auxilliary_args,
                                params.data());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();
                            m_tuner_depletants_phase1->end();

                            // phase 2, reinsert into excluded volume of i's neighbors
                            m_tuner_depletants_phase2->begin();
                            param = m_tuner_depletants_phase2->getParam();
                            args.block_size = param/1000000;
                            implicit_args.depletants_per_thread = (param % 1000000)/10000;
                            args.tpp = param%10000;
                            gpu::hpmc_depletants_auxilliary_phase2<Shape>(args,
                                implicit_args,
                                auxilliary_args,
                                params.data());
                            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                CHECK_CUDA_ERROR();
                            m_tuner_depletants_phase2->end();

                            // wait for worker streams to complete
                            for (int idev = gpu_map.size() - 1; idev >= 0; --idev)
                                {
                                hipStream_t parent = m_depletant_streams[this->m_depletant_idx(itype,jtype)][idev];
                                hipStream_t s1 = m_depletant_streams_phase1[this->m_depletant_idx(itype,jtype)][idev];
                                hipStream_t s2 = m_depletant_streams_phase2[this->m_depletant_idx(itype,jtype)][idev];
                                hipSetDevice(gpu_map[idev]);
                                hipEventRecord(m_sync_phase1[this->m_depletant_idx(itype,jtype)][idev], s1);
                                hipEventRecord(m_sync_phase2[this->m_depletant_idx(itype,jtype)][idev], s2);
                                hipStreamWaitEvent(parent, m_sync_phase1[this->m_depletant_idx(itype,jtype)][idev], 0);
                                hipStreamWaitEvent(parent, m_sync_phase2[this->m_depletant_idx(itype,jtype)][idev], 0);
                                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                                    CHECK_CUDA_ERROR();
                                }

                            ntrial_offset += ntrial*2*this->m_pdata->getMaxN();
                            }
                        }
                    }

                this->m_exec_conf->endMultiGPU();

                // did the dynamically allocated shared memory overflow during kernel execution?
                ArrayHandle<unsigned int> h_req_len(m_req_len, access_location::host, access_mode::read);

                if (*h_req_len.data > m_max_len)
                    {
                    this->m_exec_conf->msg->notice(9) << "Increasing shared mem list size per group "
                        << m_max_len << "->" << *h_req_len.data << std::endl;
                    m_max_len = *h_req_len.data;
                    continue; // rerun kernels
                    }
                #endif

                if (this->m_patch && !this->m_patch_log)
                    {
                    // access data for proposed moves
                    ArrayHandle<Scalar4> d_trial_postype(m_trial_postype, access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_trial_orientation(m_trial_orientation, access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_trial_move_type(m_trial_move_type, access_location::device, access_mode::read);

                    // access the particle data
                    ArrayHandle<Scalar4> d_postype(this->m_pdata->getPositions(), access_location::device, access_mode::read);
                    ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::read);

                    ArrayHandle<Scalar> d_charge(this->m_pdata->getCharges(), access_location::device, access_mode::read);
                    ArrayHandle<Scalar> d_diameter(this->m_pdata->getDiameters(), access_location::device, access_mode::read);
                    ArrayHandle<Scalar> d_additive_cutoff(m_additive_cutoff, access_location::device, access_mode::read);

                    /*
                     *  evaluate energy of old and new configuration simultaneously against the old and the new configuration
                     */
                    ArrayHandle<unsigned int> d_update_order_by_ptl(m_update_order.get(), access_location::device, access_mode::read);
                    ArrayHandle<unsigned int> d_reject_out_of_cell(m_reject_out_of_cell, access_location::device, access_mode::read);

                    // inequalities
                    ArrayHandle<unsigned int> d_n_inequality(this->m_n_inequality, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_inequality_literals(this->m_inequality_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_req_n_inequality(this->m_req_n_inequality, access_location::device, access_mode::readwrite);
                    ArrayHandle<double> d_coeff(this->m_coeff, access_location::device, access_mode::readwrite);
                    ArrayHandle<double> d_rhs(this->m_rhs, access_location::device, access_mode::readwrite);

                    // CNF
                    ArrayHandle<unsigned int> d_literals(this->m_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_n_literals(this->m_n_literals, access_location::device, access_mode::readwrite);
                    ArrayHandle<unsigned int> d_req_n_literals(this->m_req_n_literals, access_location::device, access_mode::readwrite);

                    PatchEnergy::gpu_args_t patch_args(
                        d_postype.data,
                        d_orientation.data,
                        d_trial_postype.data,
                        d_trial_orientation.data,
                        d_trial_move_type.data,
                        this->m_cl->getCellIndexer(),
                        this->m_cl->getDim(),
                        ghost_width,
                        this->m_pdata->getN(),
                        this->m_seed,
                        timestep,
                        this->m_exec_conf->getRank()*this->m_nselect + i,
                        this->m_pdata->getNTypes(),
                        box,
                        d_excell_idx.data,
                        d_excell_size.data,
                        m_excell_list_indexer,
                        this->m_patch->getRCut(),
                        d_additive_cutoff.data,
                        d_update_order_by_ptl.data,
                        d_charge.data,
                        d_diameter.data,
                        d_reject_out_of_cell.data,
                        d_n_inequality.data,
                        d_inequality_literals.data,
                        m_max_n_inequality,
                        d_req_n_inequality.data,
                        d_coeff.data,
                        d_rhs.data,
                        d_n_literals.data,
                        d_literals.data,
                        m_max_n_literals,
                        d_req_n_literals.data,
                        this->m_pdata->getGPUPartition());

                    // compute patch energy on default stream
                    this->m_patch->computePatchEnergyGPU(patch_args, 0);
                    } // end patch energy

                // CNF
                ArrayHandle<unsigned int> d_literals(this->m_literals, access_location::device, access_mode::readwrite);
                ArrayHandle<unsigned int> d_n_literals(this->m_n_literals, access_location::device, access_mode::readwrite);
                ArrayHandle<unsigned int> d_req_n_literals(this->m_req_n_literals, access_location::device, access_mode::readwrite);

                // inequalities
                ArrayHandle<unsigned int> d_n_inequality(this->m_n_inequality, access_location::device, access_mode::readwrite);
                ArrayHandle<unsigned int> d_inequality_literals(this->m_inequality_literals, access_location::device, access_mode::readwrite);
                ArrayHandle<unsigned int> d_req_n_inequality(this->m_req_n_inequality, access_location::device, access_mode::readwrite);
                ArrayHandle<double> d_coeff(this->m_coeff, access_location::device, access_mode::readwrite);
                ArrayHandle<double> d_rhs(this->m_rhs, access_location::device, access_mode::readwrite);

                // Add implications for unconstrained variables, tying them to false (accept)
                unsigned int nvar = this->m_pdata->getN();
                gpu::complete_cnf(nvar,
                    d_literals.data,
                    d_n_literals.data,
                    m_max_n_literals,
                    d_req_n_literals.data,
                    d_req_n_inequality.data,
                    d_inequality_literals.data,
                    d_n_inequality.data,
                    m_max_n_inequality,
                    d_coeff.data,
                    d_rhs.data);
                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                    CHECK_CUDA_ERROR();
                } while (checkReallocate());

           #if 0
           if (have_depletants && have_auxilliary_variables)
                {
                #ifdef ENABLE_MPI
                if (m_ntrial_comm)
                    {
                    // reduce free energy across rows (depletants)
                    #ifdef ENABLE_MPI_CUDA
                    ArrayHandle<int> d_deltaF_int(m_deltaF_int, access_location::device, access_mode::readwrite);
                    MPI_Allreduce(MPI_IN_PLACE,
                        d_deltaF_int.data,
                        this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements(),
                        MPI_INT,
                        MPI_SUM,
                        (*m_ntrial_comm)());
                    #else
                    ArrayHandle<int> h_deltaF_int(m_deltaF_int, access_location::host, access_mode::readwrite);
                    MPI_Allreduce(MPI_IN_PLACE,
                        h_deltaF_int.data,
                        this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements(),
                        MPI_INT,
                        MPI_SUM,
                        (*m_ntrial_comm)());
                    #endif
                    }
                #endif

                #ifdef ENABLE_MPI
                if (m_particle_comm)
                    {
                    // reduce free energy across columns (particles)
                    #ifdef ENABLE_MPI_CUDA
                    ArrayHandle<int> d_deltaF_int(m_deltaF_int, access_location::device, access_mode::readwrite);
                    MPI_Allreduce(MPI_IN_PLACE,
                        d_deltaF_int.data,
                        this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements(),
                        MPI_INT,
                        MPI_SUM,
                        (*m_particle_comm)());
                    #else
                    ArrayHandle<int> h_deltaF_int(m_deltaF_int, access_location::host, access_mode::readwrite);
                    MPI_Allreduce(MPI_IN_PLACE,
                        h_deltaF_int.data,
                        this->m_pdata->getMaxN()*this->m_depletant_idx.getNumElements(),
                        MPI_INT,
                        MPI_SUM,
                        (*m_particle_comm)());
                    #endif
                    }
                #endif

                // did the dynamically allocated shared memory overflow during kernel execution?
                ArrayHandle<unsigned int> h_req_len(m_req_len, access_location::host, access_mode::read);

                if (*h_req_len.data > m_max_len)
                    {
                    this->m_exec_conf->msg->notice(9) << "Increasing shared mem list size per group "
                        << m_max_len << "->" << *h_req_len.data << std::endl;
                    m_max_len = *h_req_len.data;
                    continue; // rerun kernels
                    }

                // final tally, do Metropolis-Hastings
                ArrayHandle<Scalar> d_fugacity(this->m_fugacity, access_location::device, access_mode::read);
                ArrayHandle<unsigned int> d_ntrial(this->m_ntrial, access_location::device, access_mode::read);
                ArrayHandle<int> d_deltaF_int(m_deltaF_int, access_location::device, access_mode::read);
                ArrayHandle<unsigned int> d_reject_out(m_reject_out, access_location::device, access_mode::readwrite);

                this->m_exec_conf->beginMultiGPU();
                m_tuner_depletants_accept->begin();
                gpu::hpmc_depletants_accept(
                    this->m_seed,
                    timestep,
                    this->m_exec_conf->getRank()*this->m_nselect + i,
                    d_deltaF_int.data,
                    this->m_depletant_idx,
                    this->m_pdata->getMaxN(),
                    d_fugacity.data,
                    d_ntrial.data,
                    d_reject_out.data,
                    this->m_pdata->getGPUPartition(),
                    m_tuner_depletants_accept->getParam());
                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                    CHECK_CUDA_ERROR();
                m_tuner_depletants_accept->end();
                this->m_exec_conf->endMultiGPU();
                }
            #endif

            unsigned int nvariables = this->m_pdata->getN();
            unsigned int nliterals = 2*nvariables;
            m_component_ptr.resize(nvariables);
            m_n_columns.resize(nvariables);
            m_csr_row_ptr.resize(nvariables+1);
            m_work.resize(nvariables);

            unsigned int max_nedges = 2*m_literals.getNumElements() + 2*m_inequality_literals.getNumElements();
            m_colidx_table.resize(max_nedges);
            m_colidx.resize(max_nedges);

            if (this->m_prof) this->m_prof->push(this->m_exec_conf, "SAT");

            // CNF
            ArrayHandle<unsigned int> d_literals(this->m_literals, access_location::device, access_mode::read);
            ArrayHandle<unsigned int> d_n_literals(this->m_n_literals, access_location::device, access_mode::read);

            // inequalities
            ArrayHandle<unsigned int> d_inequality_literals(this->m_inequality_literals, access_location::device, access_mode::readwrite);
            ArrayHandle<unsigned int> d_n_inequality(this->m_n_inequality, access_location::device, access_mode::read);
            ArrayHandle<double> d_rhs(this->m_rhs, access_location::device, access_mode::readwrite);
            ArrayHandle<double> d_coeff(this->m_coeff, access_location::device, access_mode::readwrite);

            ArrayHandle<unsigned int> d_colidx(m_colidx, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_colidx_table(m_colidx_table, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_csr_row_ptr(m_csr_row_ptr, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_n_columns(m_n_columns, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_work(m_work, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_component_ptr(m_component_ptr, access_location::device, access_mode::overwrite);

            // preprocessing of inequalities
            m_tuner_inequalities->begin();
            gpu::preprocess_inequalities(
                nvariables,
                m_max_n_inequality,
                d_inequality_literals.data,
                d_n_inequality.data,
                d_coeff.data,
                d_rhs.data,
                m_tuner_inequalities->getParam());
            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                CHECK_CUDA_ERROR();
            m_tuner_inequalities->end();

            // separate into connected components
            m_tuner_dependencies->begin();
            unsigned int param = m_tuner_dependencies->getParam();
            unsigned int block_size = param / 100;
            unsigned int literals_per_block = param % 100;

            gpu::identify_connected_components(
                m_max_n_literals,
                d_literals.data,
                d_n_literals.data,
                d_n_inequality.data,
                d_inequality_literals.data,
                m_max_n_inequality,
                d_n_columns.data,
                d_colidx_table.data,
                d_colidx.data,
                d_csr_row_ptr.data,
                nvariables,
                d_component_ptr.data,
                d_work.data,
                this->m_exec_conf->dev_prop,
                block_size,
                literals_per_block,
                this->m_exec_conf->getCachedAllocator());

            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                CHECK_CUDA_ERROR();
            m_tuner_dependencies->end();

            m_watch.resize(nliterals);
            m_next_clause.resize(nvariables*m_max_n_literals);
            m_head.resize(nvariables);
            m_next.resize(nvariables);
            m_h.resize(nvariables);
            m_state.resize(nvariables);
            m_representative.resize(nvariables);
            m_component_size.resize(nvariables);

            m_watch_inequality.resize(nliterals);
            m_next_inequality.resize(nvariables*m_max_n_inequality);
            m_is_watching.resize(nvariables*m_max_n_inequality);
            m_inequality_begin.resize(nvariables*m_max_n_inequality);
            m_watch_sum.resize(nvariables*m_max_n_inequality);

            // temporary variables used by solver
            ArrayHandle<unsigned int> d_watch(m_watch, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_next_clause(m_next_clause, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_watch_inequality(m_watch_inequality, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_next_inequality(m_next_inequality, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_head(m_head, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_next(m_next, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_h(m_h, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_state(m_state, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_reject(m_reject, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_heap(m_heap, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_representative(m_representative, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_component_size(m_component_size, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_is_watching(m_is_watching, access_location::device, access_mode::overwrite);
            ArrayHandle<unsigned int> d_inequality_begin(m_inequality_begin, access_location::device, access_mode::overwrite);
            ArrayHandle<double> d_watch_sum(m_watch_sum, access_location::device, access_mode::overwrite);

            m_tuner_mem->begin();
            param = m_tuner_mem->getParam();
            block_size = param / 100;
            literals_per_block = param % 100;

            gpu::initialize_sat_mem(
                d_watch.data,
                d_next_clause.data,
                d_head.data,
                d_next.data,
                m_max_n_literals,
                d_literals.data,
                d_n_literals.data,
                d_reject.data,
                nvariables,
                d_component_ptr.data,
                d_representative.data,
                d_component_size.data,
                d_heap.data,
                m_max_n_inequality,
                d_inequality_literals.data,
                d_n_inequality.data,
                d_coeff.data,
                d_rhs.data,
                d_inequality_begin.data,
                d_is_watching.data,
                d_watch_inequality.data,
                d_next_inequality.data,
                d_watch_sum.data,
                block_size,
                literals_per_block);
            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                CHECK_CUDA_ERROR();
            m_tuner_mem->end();

            m_tuner_sat->begin();
            gpu::solve_sat(
                d_watch.data,
                d_next_clause.data,
                d_head.data,
                d_next.data,
                d_h.data,
                d_state.data,
                m_max_n_literals,
                d_literals.data,
                d_n_literals.data,
                d_reject.data,
                nvariables,
                d_component_ptr.data,
                d_representative.data,
                d_component_size.data,
                d_heap.data,
                d_watch_inequality.data,
                d_next_inequality.data,
                d_inequality_literals.data,
                d_inequality_begin.data,
                d_is_watching.data,
                d_watch_sum.data,
                d_coeff.data,
                d_rhs.data,
                m_tuner_sat->getParam());
            if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                CHECK_CUDA_ERROR();
            m_tuner_sat->end();

            if (this->m_prof) this->m_prof->pop(this->m_exec_conf);

            if (this->m_prof) this->m_prof->push(this->m_exec_conf, "Update particle data");

                {
                // access data for proposed moves
                ArrayHandle<Scalar4> d_trial_postype(m_trial_postype, access_location::device, access_mode::read);
                ArrayHandle<Scalar4> d_trial_orientation(m_trial_orientation, access_location::device, access_mode::read);
                ArrayHandle<Scalar4> d_trial_vel(m_trial_vel, access_location::device, access_mode::read);
                ArrayHandle<unsigned int> d_trial_move_type(m_trial_move_type, access_location::device, access_mode::read);

                // access the particle data
                ArrayHandle<Scalar4> d_postype(this->m_pdata->getPositions(), access_location::device, access_mode::readwrite);
                ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::readwrite);
                ArrayHandle<Scalar4> d_vel(this->m_pdata->getVelocities(), access_location::device, access_mode::readwrite);

                // MC counters
                ArrayHandle<hpmc_counters_t> d_counters(this->m_count_total, access_location::device, access_mode::readwrite);
                ArrayHandle<hpmc_counters_t> d_counters_per_device(this->m_counters, access_location::device, access_mode::readwrite);

                // flags
                ArrayHandle<unsigned int> d_reject(m_reject, access_location::device, access_mode::read);
                ArrayHandle<unsigned int> d_reject_out_of_cell(m_reject_out_of_cell, access_location::device, access_mode::read);

                // Update the particle data and statistics
                this->m_exec_conf->beginMultiGPU();
                m_tuner_update_pdata->begin();
                gpu::hpmc_update_args_t args(
                    d_postype.data,
                    d_orientation.data,
                    d_vel.data,
                    ngpu > 1 ? d_counters_per_device.data : d_counters.data,
                    this->m_counters.getPitch(),
                    this->m_pdata->getGPUPartition(),
                    have_auxilliary_variables,
                    d_trial_postype.data,
                    d_trial_orientation.data,
                    d_trial_vel.data,
                    d_trial_move_type.data,
                    d_reject.data,
                    m_tuner_update_pdata->getParam()
                    );
                gpu::hpmc_update_pdata<Shape>(args, params.data());
                if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
                    CHECK_CUDA_ERROR();
                m_tuner_update_pdata->end();
                this->m_exec_conf->endMultiGPU();
                }

            if (this->m_prof) this->m_prof->pop(this->m_exec_conf);
            } // end loop over nselect

        if (ngpu > 1)
            {
            // reduce per-device counters
            ArrayHandle<hpmc_counters_t> d_count_total(this->m_count_total, access_location::device, access_mode::readwrite);
            ArrayHandle<hpmc_counters_t> d_counters_per_device(m_counters, access_location::device, access_mode::read);
            ArrayHandle<hpmc_implicit_counters_t> d_implicit_count_total(this->m_implicit_count, access_location::device, access_mode::readwrite);
            ArrayHandle<hpmc_implicit_counters_t> d_implicit_counters_per_device(m_implicit_counters, access_location::device, access_mode::read);

            gpu::reduce_counters(this->m_exec_conf->getNumActiveGPUs(),
                m_counters.getPitch(),
                d_counters_per_device.data,
                d_count_total.data,
                m_implicit_counters.getPitch(),
                this->m_depletant_idx,
                d_implicit_counters_per_device.data,
                d_implicit_count_total.data);
            }
        }

    // shift particles
    Scalar3 shift = make_scalar3(0,0,0);
    hoomd::UniformDistribution<Scalar> uniform(-this->m_nominal_width/Scalar(2.0),this->m_nominal_width/Scalar(2.0));
    shift.x = uniform(rng);
    shift.y = uniform(rng);
    if (this->m_sysdef->getNDimensions() == 3)
        {
        shift.z = uniform(rng);
        }

    if (this->m_pdata->getN() > 0)
        {
        BoxDim box = this->m_pdata->getBox();

        // access the particle data
        ArrayHandle<Scalar4> d_postype(this->m_pdata->getPositions(), access_location::device, access_mode::readwrite);
        ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::readwrite);
        ArrayHandle<int3> d_image(this->m_pdata->getImages(), access_location::device, access_mode::readwrite);

        gpu::hpmc_shift(d_postype.data,
                               d_image.data,
                               this->m_pdata->getN(),
                               box,
                               shift,
                               128);
        }
    if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    // update the particle data origin
    this->m_pdata->translateOrigin(shift);

    if (this->m_prof) this->m_prof->pop(this->m_exec_conf);

    this->communicate(true);

    // all particle have been moved, the aabb tree is now invalid
    this->m_aabb_tree_invalid = true;
    }

template< class Shape >
void IntegratorHPMCMonoGPU< Shape >::initializeExcellMem()
    {
    this->m_exec_conf->msg->notice(4) << "hpmc resizing expanded cells" << std::endl;

    // get the current cell dimensions
    unsigned int num_cells = this->m_cl->getCellIndexer().getNumElements();
    unsigned int num_adj = this->m_cl->getCellAdjIndexer().getW();
    unsigned int n_cell_list = this->m_cl->getPerDevice() ? this->m_exec_conf->getNumActiveGPUs() : 1;
    unsigned int num_max = this->m_cl->getNmax()*n_cell_list;

    // make the excell dimensions the same, but with room for Nmax*Nadj in each cell
    m_excell_list_indexer = Index2D(num_max * num_adj, num_cells);

    // reallocate memory
    m_excell_idx.resize(m_excell_list_indexer.getNumElements());
    m_excell_size.resize(num_cells);

    #if defined(__HIP_PLATFORM_NVCC__) && 0 // excell is currently not multi-GPU optimized, let the CUDA driver figure this out
    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // set memory hints
        auto gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            cudaMemAdvise(m_excell_idx.get(), sizeof(unsigned int)*m_excell_idx.getNumElements(), cudaMemAdviseSetAccessedBy, gpu_map[idev]);
            cudaMemAdvise(m_excell_size.get(), sizeof(unsigned int)*m_excell_size.getNumElements(), cudaMemAdviseSetAccessedBy, gpu_map[idev]);
            CHECK_CUDA_ERROR();
            }
        }
    #endif
    }

template< class Shape >
void IntegratorHPMCMonoGPU< Shape >::slotNumTypesChange()
    {
    unsigned int old_ntypes = this->m_params.size();

    // call base class method
    IntegratorHPMCMono<Shape>::slotNumTypesChange();

    // skip the reallocation if the number of types does not change
    // this keeps shape parameters when restoring a snapshot
    // it will result in invalid coefficients if the snapshot has a different type id -> name mapping
    if (this->m_pdata->getNTypes() != old_ntypes)
        {
        unsigned int ntypes = this->m_pdata->getNTypes();

        // resize array
        GlobalArray<Scalar> lambda(ntypes*this->m_depletant_idx.getNumElements(), this->m_exec_conf);
        m_lambda.swap(lambda);
        TAG_ALLOCATION(m_lambda);

        // ntypes*ntypes counters per GPU, separated by at least a memory page
        unsigned int pitch = (getpagesize() + sizeof(hpmc_implicit_counters_t)-1)/sizeof(hpmc_implicit_counters_t);
        GlobalArray<hpmc_implicit_counters_t>(std::max(pitch, this->m_implicit_count.getNumElements()),
            this->m_exec_conf->getNumActiveGPUs(), this->m_exec_conf).swap(m_implicit_counters);
        TAG_ALLOCATION(m_implicit_counters);

        #ifdef __HIP_PLATFORM_NVCC__
        if (this->m_exec_conf->allConcurrentManagedAccess())
            {
            // memory hint for overlap matrix
            cudaMemAdvise(this->m_overlaps.get(), sizeof(unsigned int)*this->m_overlaps.getNumElements(), cudaMemAdviseSetReadMostly, 0);
            CHECK_CUDA_ERROR();
            }
        #endif

        // destroy old streams
        for (auto s: m_depletant_streams)
            {
            for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                {
                hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
                hipStreamDestroy(s[idev]);
                }
            }

        // create new ones
        m_depletant_streams.resize(this->m_depletant_idx.getNumElements());
        for (unsigned int itype = 0; itype < this->m_pdata->getNTypes(); ++itype)
            {
            for (unsigned int jtype = 0; jtype < this->m_pdata->getNTypes(); ++jtype)
                {
                m_depletant_streams[this->m_depletant_idx(itype,jtype)].resize(this->m_exec_conf->getNumActiveGPUs());
                for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                    {
                    hipSetDevice(this->m_exec_conf->getGPUIds()[idev]);
                    hipStreamCreate(&m_depletant_streams[this->m_depletant_idx(itype,jtype)][idev]);
                    }
                }
            }

        GlobalArray<Scalar> additive_cutoff(ntypes*ntypes, this->m_exec_conf);
        m_additive_cutoff.swap(additive_cutoff);
        TAG_ALLOCATION(m_additive_cutoff);
        }
    }

template< class Shape >
void IntegratorHPMCMonoGPU< Shape >::updateCellWidth()
    {
    // call base class method
    IntegratorHPMCMono<Shape>::updateCellWidth();

    // update the cell list
    this->m_cl->setNominalWidth(this->m_nominal_width);

    #ifdef __HIP_PLATFORM_NVCC__
    // set memory hints
    cudaMemAdvise(this->m_params.data(), this->m_params.size()*sizeof(typename Shape::param_type), cudaMemAdviseSetReadMostly, 0);
    CHECK_CUDA_ERROR();
    #endif

    // sync up so we can access the parameters
    hipDeviceSynchronize();

    for (unsigned int i = 0; i < this->m_pdata->getNTypes(); ++i)
        {
        // attach nested memory regions
        this->m_params[i].set_memory_hint();
        CHECK_CUDA_ERROR();
        }

    // reinitialize poisson means array
    ArrayHandle<Scalar> h_lambda(m_lambda, access_location::host, access_mode::overwrite);

    for (unsigned int i_type = 0; i_type < this->m_pdata->getNTypes(); ++i_type)
        {
        Shape shape_i(quat<Scalar>(), this->m_params[i_type]);
        Scalar d_i(shape_i.getCircumsphereDiameter());

        for (unsigned int j_type = 0; j_type < this->m_pdata->getNTypes(); ++j_type)
            {
            Shape shape_j(quat<Scalar>(), this->m_params[j_type]);
            Scalar d_j(shape_j.getCircumsphereDiameter());

            // we use the larger of the two diameters for insertion
            Scalar range = std::max(d_i,d_j);

            for (unsigned int k_type = 0; k_type < this->m_pdata->getNTypes(); ++k_type)
                {
                // parameter for Poisson distribution
                Shape shape_k(quat<Scalar>(), this->m_params[k_type]);

                // get OBB and extend by depletant radius
                detail::OBB obb = shape_k.getOBB(vec3<Scalar>(0,0,0));
                obb.lengths.x += 0.5*range;
                obb.lengths.y += 0.5*range;
                if (this->m_sysdef->getNDimensions() == 3)
                    obb.lengths.z += 0.5*range;
                else
                    obb.lengths.z = 0.5; // unit length

                Scalar lambda = std::abs(this->m_fugacity[this->m_depletant_idx(i_type,j_type)]*
                    obb.getVolume(this->m_sysdef->getNDimensions()));
                h_lambda.data[k_type*this->m_depletant_idx.getNumElements()+
                    this->m_depletant_idx(i_type,j_type)] = lambda;
                }
            }
        }
    }

template<class Shape>
bool IntegratorHPMCMonoGPU< Shape >::checkReallocate()
    {
    if (this->m_prof) this->m_prof->push(this->m_exec_conf, "reallocate");
    ArrayHandle<unsigned int> h_req_n_literals(m_req_n_literals, access_location::host, access_mode::read);
    unsigned int req_maxn = *h_req_n_literals.data;
    if (req_maxn > m_max_n_literals)
        {
        m_max_n_literals = req_maxn;
        }
    unsigned int req_size_literals = m_max_n_literals*this->m_pdata->getMaxN();

    // resize CNF literals
    bool reallocate = req_size_literals > m_literals.getNumElements();
    if (reallocate)
        {
        this->m_exec_conf->msg->notice(9) << "hpmc resizing literals " << m_literals.getNumElements() << " -> " << req_size_literals << std::endl;

        GlobalArray<unsigned int> literals(req_size_literals, this->m_exec_conf);
        m_literals.swap(literals);
        TAG_ALLOCATION(m_literals);
        }

    ArrayHandle<unsigned int> h_req_n_inequality(m_req_n_inequality, access_location::host, access_mode::read);
    unsigned int req_maxn_inequality = *h_req_n_inequality.data;
    if (req_maxn_inequality > m_max_n_inequality)
        {
        m_max_n_inequality = req_maxn_inequality;
        }
    unsigned int req_size_inequalities = m_max_n_inequality*this->m_pdata->getMaxN();

    // resize Pseudo-Boolean literals
    bool reallocate_inequalities = req_size_inequalities > m_inequality_literals.getNumElements();
    if (reallocate_inequalities)
        {
        this->m_exec_conf->msg->notice(9) << "hpmc resizing inequalities " << m_inequality_literals.getNumElements()
            << " -> " << req_size_inequalities << std::endl;

        GlobalArray<unsigned int> inequality_literals(req_size_inequalities, this->m_exec_conf);
        m_inequality_literals.swap(inequality_literals);
        TAG_ALLOCATION(m_inequality_literals);

        GlobalArray<double> coeff(req_size_inequalities, this->m_exec_conf);
        m_coeff.swap(coeff);
        TAG_ALLOCATION(m_coeff);

        GlobalArray<double> rhs(req_size_inequalities, this->m_exec_conf);
        m_rhs.swap(rhs);
        TAG_ALLOCATION(m_rhs);
        }
    if (this->m_prof) this->m_prof->pop(this->m_exec_conf);

    return reallocate || reallocate_inequalities;
    }

#ifdef ENABLE_MPI
template<class Shape>
std::vector<hpmc_implicit_counters_t> IntegratorHPMCMonoGPU<Shape>::getImplicitCounters(unsigned int mode)
    {
    std::vector<hpmc_implicit_counters_t> result = IntegratorHPMCMono<Shape>::getImplicitCounters(mode);

    if (m_ntrial_comm)
        {
        // MPI Reduction to total result values on all ranks
        for (unsigned int i = 0; i < this->m_depletant_idx.getNumElements(); ++i)
            {
            MPI_Allreduce(MPI_IN_PLACE, &result[i].insert_count, 1, MPI_LONG_LONG_INT, MPI_SUM, (*m_ntrial_comm)());
            MPI_Allreduce(MPI_IN_PLACE, &result[i].insert_accept_count, 1, MPI_LONG_LONG_INT, MPI_SUM, (*m_ntrial_comm)());
            MPI_Allreduce(MPI_IN_PLACE, &result[i].insert_accept_count_sq, 1, MPI_LONG_LONG_INT, MPI_SUM, (*m_ntrial_comm)());
            }
        }

    return result;
    }
#endif

//! Export this hpmc integrator to python
/*! \param name Name of the class in the exported python module
    \tparam Shape An instantiation of IntegratorHPMCMono<Shape> will be exported
*/
template < class Shape > void export_IntegratorHPMCMonoGPU(pybind11::module& m, const std::string& name)
    {
     pybind11::class_<IntegratorHPMCMonoGPU<Shape>, IntegratorHPMCMono<Shape>,
        std::shared_ptr< IntegratorHPMCMonoGPU<Shape> > >(m, name.c_str())
              .def(pybind11::init< std::shared_ptr<SystemDefinition>, std::shared_ptr<CellList>, unsigned int >())
              #ifdef ENABLE_MPI
              .def("setNtrialCommunicator", &IntegratorHPMCMonoGPU<Shape>::setNtrialCommunicator)
              .def("setParticleCommunicator", &IntegratorHPMCMonoGPU<Shape>::setParticleCommunicator)
              #endif
              ;
    }

} // end namespace hpmc

#endif // ENABLE_HIP
