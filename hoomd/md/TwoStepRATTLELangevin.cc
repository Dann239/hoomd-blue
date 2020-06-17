// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

#include "TwoStepRATTLELangevin.h"
#include "hoomd/RandomNumbers.h"
#include "hoomd/RNGIdentifiers.h"
#include "hoomd/VectorMath.h"

inline Scalar maxNorm(Scalar3 vec, Scalar resid)
    {
    Scalar vec_norm = sqrt(dot(vec,vec));
    Scalar abs_resid = fabs(resid);
    if ( vec_norm > abs_resid) return vec_norm;
    else return abs_resid;
    }


#ifdef ENABLE_MPI
#include "hoomd/HOOMDMPI.h"
#endif

namespace py = pybind11;
using namespace std;
using namespace hoomd;

/*! \file TwoStepRATTLELangevin.h
    \brief Contains code for the TwoStepRATTLELangevin class
    \Warning NDOF is still 3*(N_part-1) and not 2*(N_part-1)!!! Has to be considered in thermodynamic quantities calculations.
*/

/*! \param sysdef SystemDefinition this method will act on. Must not be NULL.
    \param group The group of particles this integration method is to work on
    \param manifold The manifold describing the constraint during the RATTLE integration method
    \param T Temperature set point as a function of time
    \param seed Random seed to use in generating random numbers
    \param use_lambda If true, gamma=lambda*diameter, otherwise use a per-type gamma via setGamma()
    \param lambda Scale factor to convert diameter to gamma
    \param noiseless_t If set true, there will be no translational noise (random force)
    \param noiseless_r If set true, there will be no rotational noise (random torque)
    \param eta Tolerance for the RATTLE iteration algorithm
    \param suffix Suffix to attach to the end of log quantity names

*/
TwoStepRATTLELangevin::TwoStepRATTLELangevin(std::shared_ptr<SystemDefinition> sysdef,
                           std::shared_ptr<ParticleGroup> group,
                           std::shared_ptr<Manifold> manifold,
                           std::shared_ptr<Variant> T,
                           unsigned int seed,
                           bool use_lambda,
                           Scalar lambda,
                           bool noiseless_t,
                           bool noiseless_r,
                           Scalar eta,
                           const std::string& suffix)
    : TwoStepLangevinBase(sysdef, group, T, seed, use_lambda, lambda), m_manifold(manifold), m_reservoir_energy(0),  m_extra_energy_overdeltaT(0),
      m_tally(false), m_noiseless_t(noiseless_t), m_noiseless_r(noiseless_r), m_eta(eta)
    {
    m_exec_conf->msg->notice(5) << "Constructing TwoStepRATTLELangevin" << endl;

    m_log_name = string("langevin_reservoir_energy") + suffix;
    }

TwoStepRATTLELangevin::~TwoStepRATTLELangevin()
    {
    m_exec_conf->msg->notice(5) << "Destroying TwoStepRATTLELangevin" << endl;
    }

/*! Returns a list of log quantities this compute calculates
*/
std::vector< std::string > TwoStepRATTLELangevin::getProvidedLogQuantities()
    {
    vector<string> result;
    if (m_tally)
        result.push_back(m_log_name);
    return result;
    }

/*! \param quantity Name of the log quantity to get
    \param timestep Current time step of the simulation
    \param my_quantity_flag passed as false, changed to true if quantity logged here
*/

Scalar TwoStepRATTLELangevin::getLogValue(const std::string& quantity, unsigned int timestep, bool &my_quantity_flag)
    {
    if (m_tally && quantity == m_log_name)
        {
        my_quantity_flag = true;
        return m_reservoir_energy+m_extra_energy_overdeltaT*m_deltaT;
        }
    else
        return Scalar(0);
    }

/*! \param timestep Current time step
    \post Particle positions are moved forward to timestep+1 and velocities to timestep+1/2 per the velocity verlet
          method.
*/
void TwoStepRATTLELangevin::integrateStepOne(unsigned int timestep)
    {
    unsigned int group_size = m_group->getNumMembers();

    // profile this step
    if (m_prof)
        m_prof->push("Langevin step 1");

    ArrayHandle<Scalar4> h_vel(m_pdata->getVelocities(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar3> h_accel(m_pdata->getAccelerations(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::readwrite);
    ArrayHandle<int3> h_image(m_pdata->getImages(), access_location::host, access_mode::readwrite);

    ArrayHandle<Scalar3> h_gamma_r(m_gamma_r, access_location::host, access_mode::read);

    const BoxDim& box = m_pdata->getBox();

    // perform the first half step of the RATTLE algorithm applied on velocity verlet
    // v(t+deltaT/2) = v(t) + (1/2)*deltaT*(a-lambda*n_manifold(x(t))/m)
    // iterative: x(t+deltaT) = x(t+deltaT) - J^(-1)*residual
    for (unsigned int group_idx = 0; group_idx < group_size; group_idx++)
        {
        unsigned int j = m_group->getMemberIndex(group_idx);

	    Scalar deltaT_half = Scalar(1.0/2.0)*m_deltaT;

	    Scalar3 half_vel;
        half_vel.x = h_vel.data[j].x + deltaT_half*h_accel.data[j].x;
        half_vel.y = h_vel.data[j].y + deltaT_half*h_accel.data[j].y;
        half_vel.z = h_vel.data[j].z + deltaT_half*h_accel.data[j].z;

        h_vel.data[j].x = half_vel.x;
        h_vel.data[j].y = half_vel.y;
        h_vel.data[j].z = half_vel.z;

        Scalar dx = m_deltaT*half_vel.x;
        Scalar dy = m_deltaT*half_vel.y;
        Scalar dz = m_deltaT*half_vel.z;

        h_pos.data[j].x += dx;
        h_pos.data[j].y += dy;
        h_pos.data[j].z += dz;

        // particles may have been moved slightly outside the box by the above steps, wrap them back into place
        box.wrap(h_pos.data[j], h_image.data[j]);

        }

    if (m_aniso)
        {
        ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::readwrite);
        ArrayHandle<Scalar4> h_angmom(m_pdata->getAngularMomentumArray(), access_location::host, access_mode::readwrite);
        ArrayHandle<Scalar4> h_net_torque(m_pdata->getNetTorqueArray(), access_location::host, access_mode::read);
        ArrayHandle<Scalar3> h_inertia(m_pdata->getMomentsOfInertiaArray(), access_location::host, access_mode::read);

        for (unsigned int group_idx = 0; group_idx < group_size; group_idx++)
            {
            unsigned int j = m_group->getMemberIndex(group_idx);

            quat<Scalar> q(h_orientation.data[j]);
            quat<Scalar> p(h_angmom.data[j]);
            vec3<Scalar> t(h_net_torque.data[j]);
            vec3<Scalar> I(h_inertia.data[j]);

            // rotate torque into principal frame
            t = rotate(conj(q),t);

            // check for zero moment of inertia
            bool x_zero, y_zero, z_zero;
            x_zero = (I.x < EPSILON); y_zero = (I.y < EPSILON); z_zero = (I.z < EPSILON);

            // ignore torque component along an axis for which the moment of inertia zero
            if (x_zero) t.x = 0;
            if (y_zero) t.y = 0;
            if (z_zero) t.z = 0;

            // advance p(t)->p(t+deltaT/2), q(t)->q(t+deltaT)
            // using Trotter factorization of rotation Liouvillian
            p += m_deltaT*q*t;

            quat<Scalar> p1, p2, p3; // permutated quaternions
            quat<Scalar> q1, q2, q3;
            Scalar phi1, cphi1, sphi1;
            Scalar phi2, cphi2, sphi2;
            Scalar phi3, cphi3, sphi3;

            if (!z_zero)
                {
                p3 = quat<Scalar>(-p.v.z,vec3<Scalar>(p.v.y,-p.v.x,p.s));
                q3 = quat<Scalar>(-q.v.z,vec3<Scalar>(q.v.y,-q.v.x,q.s));
                phi3 = Scalar(1./4.)/I.z*dot(p,q3);
                cphi3 = slow::cos(Scalar(1./2.)*m_deltaT*phi3);
                sphi3 = slow::sin(Scalar(1./2.)*m_deltaT*phi3);

                p=cphi3*p+sphi3*p3;
                q=cphi3*q+sphi3*q3;
                }

            if (!y_zero)
                {
                p2 = quat<Scalar>(-p.v.y,vec3<Scalar>(-p.v.z,p.s,p.v.x));
                q2 = quat<Scalar>(-q.v.y,vec3<Scalar>(-q.v.z,q.s,q.v.x));
                phi2 = Scalar(1./4.)/I.y*dot(p,q2);
                cphi2 = slow::cos(Scalar(1./2.)*m_deltaT*phi2);
                sphi2 = slow::sin(Scalar(1./2.)*m_deltaT*phi2);

                p=cphi2*p+sphi2*p2;
                q=cphi2*q+sphi2*q2;
                }

            if (!x_zero)
                {
                p1 = quat<Scalar>(-p.v.x,vec3<Scalar>(p.s,p.v.z,-p.v.y));
                q1 = quat<Scalar>(-q.v.x,vec3<Scalar>(q.s,q.v.z,-q.v.y));
                phi1 = Scalar(1./4.)/I.x*dot(p,q1);
                cphi1 = slow::cos(m_deltaT*phi1);
                sphi1 = slow::sin(m_deltaT*phi1);

                p=cphi1*p+sphi1*p1;
                q=cphi1*q+sphi1*q1;
                }

            if (! y_zero)
                {
                p2 = quat<Scalar>(-p.v.y,vec3<Scalar>(-p.v.z,p.s,p.v.x));
                q2 = quat<Scalar>(-q.v.y,vec3<Scalar>(-q.v.z,q.s,q.v.x));
                phi2 = Scalar(1./4.)/I.y*dot(p,q2);
                cphi2 = slow::cos(Scalar(1./2.)*m_deltaT*phi2);
                sphi2 = slow::sin(Scalar(1./2.)*m_deltaT*phi2);

                p=cphi2*p+sphi2*p2;
                q=cphi2*q+sphi2*q2;
                }

            if (! z_zero)
                {
                p3 = quat<Scalar>(-p.v.z,vec3<Scalar>(p.v.y,-p.v.x,p.s));
                q3 = quat<Scalar>(-q.v.z,vec3<Scalar>(q.v.y,-q.v.x,q.s));
                phi3 = Scalar(1./4.)/I.z*dot(p,q3);
                cphi3 = slow::cos(Scalar(1./2.)*m_deltaT*phi3);
                sphi3 = slow::sin(Scalar(1./2.)*m_deltaT*phi3);

                p=cphi3*p+sphi3*p3;
                q=cphi3*q+sphi3*q3;
                }

            // renormalize (improves stability)
            q = q*(Scalar(1.0)/slow::sqrt(norm2(q)));

            h_orientation.data[j] = quat_to_scalar4(q);
            h_angmom.data[j] = quat_to_scalar4(p);
            }
        }

    // done profiling
    if (m_prof)
        m_prof->pop();
    }

/*! \param timestep Current time step
    \post particle velocities are moved forward to timestep+1
*/
void TwoStepRATTLELangevin::integrateStepTwo(unsigned int timestep)
    {
    unsigned int group_size = m_group->getNumMembers();

    const GlobalArray< Scalar4 >& net_force = m_pdata->getNetForce();

    // profile this step
    if (m_prof)
        m_prof->push("Langevin step 2");

    ArrayHandle<Scalar4> h_vel(m_pdata->getVelocities(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar3> h_accel(m_pdata->getAccelerations(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar> h_diameter(m_pdata->getDiameters(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::read);
    ArrayHandle<unsigned int> h_tag(m_pdata->getTags(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_net_force(net_force, access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_gamma(m_gamma, access_location::host, access_mode::read);
    ArrayHandle<Scalar3> h_gamma_r(m_gamma_r, access_location::host, access_mode::read);

    ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar4> h_angmom(m_pdata->getAngularMomentumArray(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar4> h_net_torque(m_pdata->getNetTorqueArray(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar3> h_inertia(m_pdata->getMomentsOfInertiaArray(), access_location::host, access_mode::read);

    // grab some initial variables
    const Scalar currentTemp = m_T->getValue(timestep);

    // energy transferred over this time step
    Scalar bd_energy_transfer = 0;

    unsigned int maxiteration = 10;

    // a(t+deltaT) gets modified with the bd forces
    // v(t+deltaT) = v(t+deltaT/2) + 1/2 * a(t+deltaT)*deltaT
    // iterative: v(t+deltaT) = v(t+deltaT/2) - J^(-1)*residual
    for (unsigned int group_idx = 0; group_idx < group_size; group_idx++)
        {
        unsigned int j = m_group->getMemberIndex(group_idx);
        unsigned int ptag = h_tag.data[j];

        // Initialize the RNG
        RandomGenerator rng(RNGIdentifier::TwoStepLangevin, m_seed, ptag, timestep);

        // first, calculate the BD forces on manifold
        // Generate two random numbers
        
        Scalar rx, ry, rz, coeff;

	Scalar gamma;
	if (m_use_lambda)
	    gamma = m_lambda*h_diameter.data[j];
	else
	    {
	    unsigned int type = __scalar_as_int(h_pos.data[j].w);
	    gamma = h_gamma.data[type];
	    }

	Scalar3 normal = m_manifold->derivative(make_scalar3(h_pos.data[j].x,h_pos.data[j].y,h_pos.data[j].z));
	Scalar ndotn = dot(normal,normal);

        if(currentTemp > 0)
	{
		hoomd::UniformDistribution<Scalar> uniform(Scalar(-1), Scalar(1));

		rx = uniform(rng);
		ry = uniform(rng);
		rz = uniform(rng);

		// compute the bd force
		coeff = fast::sqrt(Scalar(6.0) *gamma*currentTemp/m_deltaT);
		if (m_noiseless_t)
		    coeff = Scalar(0.0);

		Scalar proj_x = normal.x/fast::sqrt(ndotn);
		Scalar proj_y = normal.y/fast::sqrt(ndotn);
		Scalar proj_z = normal.z/fast::sqrt(ndotn);
		
		Scalar proj_r = rx*proj_x + ry*proj_y + rz*proj_z;
		rx = rx - proj_r*proj_x;
		ry = ry - proj_r*proj_y;
		rz = rz - proj_r*proj_z;
	}
	else
	{
           	rx = 0;
           	ry = 0;
           	rz = 0;
           	coeff = 0;
	}

        Scalar bd_fx = rx*coeff - gamma*h_vel.data[j].x;
        Scalar bd_fy = ry*coeff - gamma*h_vel.data[j].y;
        Scalar bd_fz = rz*coeff - gamma*h_vel.data[j].z;

        // then, calculate acceleration from the net force
	Scalar mass = h_vel.data[j].w;
	Scalar inv_mass = Scalar(1.0)/mass;
        h_accel.data[j].x = (h_net_force.data[j].x + bd_fx)*inv_mass;
        h_accel.data[j].y = (h_net_force.data[j].y + bd_fy)*inv_mass;
        h_accel.data[j].z = (h_net_force.data[j].z + bd_fz)*inv_mass;

        Scalar mu = 0;
        Scalar inv_alpha = -Scalar(1.0/2.0)*m_deltaT;
	inv_alpha = Scalar(1.0)/inv_alpha;
   
        Scalar3 next_vel; 
        next_vel.x = h_vel.data[j].x + Scalar(1.0/2.0)*m_deltaT*h_accel.data[j].x;
        next_vel.y = h_vel.data[j].y + Scalar(1.0/2.0)*m_deltaT*h_accel.data[j].y;
        next_vel.z = h_vel.data[j].z + Scalar(1.0/2.0)*m_deltaT*h_accel.data[j].z;

        Scalar3 residual;
        Scalar resid;
        Scalar3 vel_dot;
   
        unsigned int iteration = 0;
        do
            {
            iteration++;
            vel_dot.x = h_accel.data[j].x - mu*inv_mass*normal.x;
            vel_dot.y = h_accel.data[j].y - mu*inv_mass*normal.y;
            vel_dot.z = h_accel.data[j].z - mu*inv_mass*normal.z;

            residual.x = h_vel.data[j].x - next_vel.x + Scalar(1.0/2.0)*m_deltaT*vel_dot.x;
            residual.y = h_vel.data[j].y - next_vel.y + Scalar(1.0/2.0)*m_deltaT*vel_dot.y;
            residual.z = h_vel.data[j].z - next_vel.z + Scalar(1.0/2.0)*m_deltaT*vel_dot.z;
            resid = dot(normal, next_vel)*inv_mass;

	    Scalar ndotr = dot(normal,residual);
	    Scalar ndotn = dot(normal,normal);
            Scalar beta = (mass*resid + ndotr)/ndotn;
            next_vel.x = next_vel.x - normal.x*beta + residual.x;
            next_vel.y = next_vel.y - normal.y*beta + residual.y;
            next_vel.z = next_vel.z - normal.z*beta + residual.z;
            mu =  mu - mass*beta*inv_alpha;

	    } while (maxNorm(residual,resid)*mass > m_eta && iteration < maxiteration );

        // then, update the velocity
        h_vel.data[j].x += Scalar(1.0/2.0)*m_deltaT*(h_accel.data[j].x - mu*inv_mass*normal.x);
        h_vel.data[j].y += Scalar(1.0/2.0)*m_deltaT*(h_accel.data[j].y - mu*inv_mass*normal.y);
        h_vel.data[j].z += Scalar(1.0/2.0)*m_deltaT*(h_accel.data[j].z - mu*inv_mass*normal.z);

        // tally the energy transfer from the bd thermal reservoir to the particles
        if (m_tally) bd_energy_transfer += bd_fx * h_vel.data[j].x + bd_fy * h_vel.data[j].y + bd_fz * h_vel.data[j].z;

        // rotational updates
        if (m_aniso)
            {
            unsigned int type_r = __scalar_as_int(h_pos.data[j].w);
            Scalar3 gamma_r = h_gamma_r.data[type_r];
            // get body frame ang_mom
            quat<Scalar> p(h_angmom.data[j]);
            quat<Scalar> q(h_orientation.data[j]);
            vec3<Scalar> t(h_net_torque.data[j]);
            vec3<Scalar> I(h_inertia.data[j]);

            // s is the pure imaginary quaternion with im. part equal to true angular velocity
            vec3<Scalar> s;
            s = (Scalar(1./2.) * conj(q) * p).v;

            if (gamma_r.x > 0 || gamma_r.y > 0 || gamma_r.z > 0)
                {
                // first calculate in the body frame random and damping torque imposed by the dynamics
                vec3<Scalar> bf_torque;

                // original Gaussian random torque
                Scalar3 sigma_r = make_scalar3(fast::sqrt(Scalar(2.0)*gamma_r.x*currentTemp/m_deltaT),
                                               fast::sqrt(Scalar(2.0)*gamma_r.y*currentTemp/m_deltaT),
                                               fast::sqrt(Scalar(2.0)*gamma_r.z*currentTemp/m_deltaT));
                if (m_noiseless_r) sigma_r = make_scalar3(0.0,0.0,0.0);

                Scalar rand_x = hoomd::NormalDistribution<Scalar>(sigma_r.x)(rng);
                Scalar rand_y = hoomd::NormalDistribution<Scalar>(sigma_r.y)(rng);
                Scalar rand_z = hoomd::NormalDistribution<Scalar>(sigma_r.z)(rng);

                // check for degenerate moment of inertia
                bool x_zero, y_zero, z_zero;
                x_zero = (I.x < EPSILON); y_zero = (I.y < EPSILON); z_zero = (I.z < EPSILON);

                bf_torque.x = rand_x - gamma_r.x * (s.x / I.x);
                bf_torque.y = rand_y - gamma_r.y * (s.y / I.y);
                bf_torque.z = rand_z - gamma_r.z * (s.z / I.z);

                // ignore torque component along an axis for which the moment of inertia zero
                if (x_zero) bf_torque.x = 0;
                if (y_zero) bf_torque.y = 0;
                if (z_zero) bf_torque.z = 0;

                // change to lab frame and update the net torque
                bf_torque = rotate(q, bf_torque);
                h_net_torque.data[j].x += bf_torque.x;
                h_net_torque.data[j].y += bf_torque.y;
                h_net_torque.data[j].z += bf_torque.z;

                }
            }
        }


    // then, update the angular velocity
    if (m_aniso)
        {
        // angular degrees of freedom
        for (unsigned int group_idx = 0; group_idx < group_size; group_idx++)
            {
            unsigned int j = m_group->getMemberIndex(group_idx);

            quat<Scalar> q(h_orientation.data[j]);
            quat<Scalar> p(h_angmom.data[j]);
            vec3<Scalar> t(h_net_torque.data[j]);
            vec3<Scalar> I(h_inertia.data[j]);

            // rotate torque into principal frame
            t = rotate(conj(q),t);

            // check for zero moment of inertia
            bool x_zero, y_zero, z_zero;
            x_zero = (I.x < EPSILON); y_zero = (I.y < EPSILON); z_zero = (I.z < EPSILON);

            // ignore torque component along an axis for which the moment of inertia zero
            if (x_zero) t.x = 0;
            if (y_zero) t.y = 0;
            if (z_zero) t.z = 0;

            // advance p(t+deltaT/2)->p(t+deltaT)
            p += m_deltaT*q*t;
            h_angmom.data[j] = quat_to_scalar4(p);
            }
        }


    // update energy reservoir
    if (m_tally)
        {
        #ifdef ENABLE_MPI
        if (m_comm)
            {
            MPI_Allreduce(MPI_IN_PLACE, &bd_energy_transfer, 1, MPI_HOOMD_SCALAR, MPI_SUM, m_exec_conf->getMPICommunicator());
            }
        #endif
        m_reservoir_energy -= bd_energy_transfer*m_deltaT;
        m_extra_energy_overdeltaT = 0.5*bd_energy_transfer;
        }

    // done profiling
    if (m_prof)
        m_prof->pop();
    }

void TwoStepRATTLELangevin::IncludeRATTLEForce(unsigned int timestep)
    {

    unsigned int group_size = m_group->getNumMembers();

    const GlobalArray< Scalar4 >& net_force = m_pdata->getNetForce();
    const GlobalArray<Scalar>&  net_virial = m_pdata->getNetVirial();
    ArrayHandle<Scalar4> h_vel(m_pdata->getVelocities(), access_location::host, access_mode::read);
    ArrayHandle<Scalar3> h_accel(m_pdata->getAccelerations(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::read);

    ArrayHandle<Scalar4> h_net_force(net_force, access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar> h_net_virial(net_virial, access_location::host, access_mode::readwrite);

    unsigned int net_virial_pitch = net_virial.getPitch();
    unsigned int maxiteration = 10;

    // perform the first half step of the RATTLE algorithm applied on velocity verlet
    // v(t+deltaT/2) = v(t) + (1/2)*deltaT*(a-lambda*n_manifold(x(t))/m)
    // iterative: x(t+deltaT) = x(t+deltaT) - J^(-1)*residual
    for (unsigned int group_idx = 0; group_idx < group_size; group_idx++)
        {
        unsigned int j = m_group->getMemberIndex(group_idx);

	    Scalar lambda = 0.0;
            
	    Scalar3 next_pos;
	    next_pos.x = h_pos.data[j].x;
	    next_pos.y = h_pos.data[j].y;
	    next_pos.z = h_pos.data[j].z;

	    Scalar3 normal = m_manifold->derivative(next_pos);

	    Scalar inv_mass = Scalar(1.0)/h_vel.data[j].w;
	    Scalar deltaT_half = Scalar(1.0/2.0)*m_deltaT;
	    Scalar inv_alpha = -deltaT_half*m_deltaT*inv_mass;
	    inv_alpha = Scalar(1.0)/inv_alpha;

	    Scalar3 residual;
	    Scalar resid;
	    Scalar3 half_vel;

	    unsigned int iteration = 0;
	    do
	        {
	        iteration++;
                half_vel.x = h_vel.data[j].x + deltaT_half*(h_accel.data[j].x-inv_mass*lambda*normal.x);
                half_vel.y = h_vel.data[j].y + deltaT_half*(h_accel.data[j].y-inv_mass*lambda*normal.y);
                half_vel.z = h_vel.data[j].z + deltaT_half*(h_accel.data[j].z-inv_mass*lambda*normal.z);

	        residual.x = h_pos.data[j].x - next_pos.x + m_deltaT*half_vel.x;
	        residual.y = h_pos.data[j].y - next_pos.y + m_deltaT*half_vel.y;
	        residual.z = h_pos.data[j].z - next_pos.z + m_deltaT*half_vel.z;
	        resid = m_manifold->implicit_function(next_pos);

                Scalar3 next_normal =  m_manifold->derivative(next_pos);
	        Scalar nndotr = dot(next_normal,residual);
	        Scalar nndotn = dot(next_normal,normal);
	        Scalar beta = (resid + nndotr)/nndotn;

                next_pos.x = next_pos.x - beta*normal.x + residual.x;   
                next_pos.y = next_pos.y - beta*normal.y + residual.y;   
                next_pos.z = next_pos.z - beta*normal.z + residual.z;
	        lambda = lambda - beta*inv_alpha;
	     
	        } while (maxNorm(residual,resid) > m_eta && iteration < maxiteration );

	    h_net_force.data[j].x -= lambda*normal.x;
	    h_net_force.data[j].y -= lambda*normal.y;
	    h_net_force.data[j].z -= lambda*normal.z;

        h_net_virial.data[0*net_virial_pitch+j] -= lambda*normal.x*h_pos.data[j].x;
        h_net_virial.data[1*net_virial_pitch+j] -= 0.5*lambda*(normal.y*h_pos.data[j].x + normal.x*h_pos.data[j].y);
        h_net_virial.data[2*net_virial_pitch+j] -= 0.5*lambda*(normal.z*h_pos.data[j].x + normal.x*h_pos.data[j].z);
        h_net_virial.data[3*net_virial_pitch+j] -= lambda*normal.y*h_pos.data[j].y;
        h_net_virial.data[4*net_virial_pitch+j] -= 0.5*lambda*(normal.y*h_pos.data[j].z + normal.z*h_pos.data[j].y);
        h_net_virial.data[5*net_virial_pitch+j] -= lambda*normal.z*h_pos.data[j].z;

	    h_accel.data[j].x -= inv_mass*lambda*normal.x;
	    h_accel.data[j].y -= inv_mass*lambda*normal.y;
	    h_accel.data[j].z -= inv_mass*lambda*normal.z;
        }
    }

/*! \param query_group Group over which to count (translational) degrees of freedom.
    A majority of the integration methods add D degrees of freedom per particle in \a query_group that is also in the
    group assigned to the method. Hence, the base class IntegrationMethodTwoStep will implement that counting.
    Derived classes can override if needed.
*/
unsigned int TwoStepRATTLELangevin::getNDOF(std::shared_ptr<ParticleGroup> query_group)
    {
    // get the size of the intersection between query_group and m_group
    unsigned int intersect_size = ParticleGroup::groupIntersection(query_group, m_group)->getNumMembersGlobal();

    return ( m_sysdef->getNDimensions() - 1 ) * intersect_size;
    }

void export_TwoStepRATTLELangevin(py::module& m)
    {
    py::class_<TwoStepRATTLELangevin, std::shared_ptr<TwoStepRATTLELangevin> >(m, "TwoStepRATTLELangevin", py::base<TwoStepLangevinBase>())
        .def(py::init< std::shared_ptr<SystemDefinition>,
                            std::shared_ptr<ParticleGroup>,
			    std::shared_ptr<Manifold>,
                            std::shared_ptr<Variant>,
                            unsigned int,
                            bool,
                            Scalar,
                            bool,
                            bool,
			    Scalar,
                            const std::string&>())
        .def("setTally", &TwoStepRATTLELangevin::setTally)
        ;
    }
