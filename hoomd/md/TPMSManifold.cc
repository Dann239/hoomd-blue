// Copyright (c) 2009-2020 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: pschoenhoefer

#include "TPMSManifold.h"

namespace py = pybind11;

using namespace std;

/*! \file TPMSManifold.cc
    \brief Contains code for the TPMSManifold class
*/

/*!
    \param surf Defines the specific triply periodic minimal surface
    \param Nx The number of unitcells in x-direction
    \param Ny The number of unitcells in y-direction
    \param Nz The number of unitcells in z-direction
*/

TPMSManifold::TPMSManifold(std::shared_ptr<SystemDefinition> sysdef,
                  		std::string surf, 
                  		unsigned int Nx,
                  		unsigned int Ny,
                  		unsigned int Nz)
  : Manifold(sysdef), m_Nx(Nx), m_Ny(Ny), m_Nz(Nz) 
       {
    m_exec_conf->msg->notice(5) << "Constructing TPMSManifold" << endl;

    if( surf == "G" || surf == "GYROID" )
    { 
	gyroid = true;
	m_surf = 1;
    }
    else{ 
	if( surf == "D" || surf == "DIAMOND")
		{ 
		diamond = true;
		m_surf = 2;
		}
    else{ 
	if( surf == "P" || surf == "PRIMITIVE") 
	{
		primitive = true;
		m_surf = 3;
		}
	}
    }

    setup();
       }

TPMSManifold::~TPMSManifold() 
       {
    m_exec_conf->msg->notice(5) << "Destroying TPMSManifold" << endl;
       }

        //! Return the value of the implicit surface function of the TPMSs
        /*! \param point The position to evaluate the function.
        */
Scalar TPMSManifold::implicit_function(Scalar3 point)
       {

       if(gyroid)
          return slow::sin(Lx*point.x)*slow::cos(Ly*point.y) + slow::sin(Ly*point.y)*slow::cos(Lz*point.z) + slow::sin(Lz*point.z)*slow::cos(Lx*point.x);	
       else{ 
            if(diamond)
                return slow::cos(Lx*point.x)*slow::cos(Ly*point.y)*slow::cos(Lz*point.z) - slow::sin(Lx*point.x)*slow::sin(Ly*point.y)*slow::sin(Lz*point.z);
            else
                return  slow::cos(Lx*point.x) + slow::cos(Ly*point.y) + slow::cos(Lz*point.z);
       }
       }

       //! Return the gradient of the constraint.
       /*! \param point The location to evaluate the gradient.
       */
Scalar3 TPMSManifold::derivative(Scalar3 point)
       {
       Scalar3 delta;
       if(gyroid){
          delta.x = Lx*(slow::cos(Lx*point.x)*slow::cos(Ly*point.y) - slow::sin(Lz*point.z)*slow::sin(Lx*point.x));
          delta.y = Ly*(slow::cos(Ly*point.y)*slow::cos(Lz*point.z) - slow::sin(Lx*point.x)*slow::sin(Ly*point.y));	
          delta.z = Lz*(slow::cos(Lz*point.z)*slow::cos(Lx*point.x) - slow::sin(Ly*point.y)*slow::sin(Lz*point.z));	
       }else{
              if(diamond){
                  delta.x = -Lx*(slow::sin(Lx*point.x)*slow::cos(Ly*point.y)*slow::cos(Lz*point.z) + slow::cos(Lx*point.x)*slow::sin(Ly*point.y)*slow::sin(Lz*point.z));
                  delta.y = -Ly*(slow::cos(Lx*point.x)*slow::sin(Ly*point.y)*slow::cos(Lz*point.z) + slow::sin(Lx*point.x)*slow::cos(Ly*point.y)*slow::sin(Lz*point.z));
                  delta.z = -Lz*(slow::cos(Lx*point.x)*slow::cos(Ly*point.y)*slow::sin(Lz*point.z) + slow::sin(Lx*point.x)*slow::sin(Ly*point.y)*slow::cos(Lz*point.z));
              }else{
                 delta.x = -Lx*slow::sin(Lx*point.x);
                 delta.y = -Ly*slow::sin(Ly*point.y);
                 delta.z = -Lz*slow::sin(Lz*point.z);
              }
       }	
       return delta;
       }

Scalar3 TPMSManifold::returnL()
    {
	Scalar3 L;
	L.x=Lx;
	L.y=Ly;
	L.z=Lz;
	return L;
    }

void TPMSManifold::setup()
    {


    BoxDim box = m_pdata->getGlobalBox();
    Scalar3 box_length = box.getHi() - box.getLo();

    Lx = M_PI*m_Nx/box_length.x;
    Ly = M_PI*m_Ny/box_length.y;
    Lz = M_PI*m_Nz/box_length.z;
    
   if(!diamond){
       Lx *= Scalar(2.0);
       Ly *= Scalar(2.0);
       Lz *= Scalar(2.0);
   }

    }

//! Exports the TPMSManifold class to python
void export_TPMSManifold(pybind11::module& m)
    {
    py::class_< TPMSManifold, std::shared_ptr<TPMSManifold> >(m, "TPMSManifold", py::base<Manifold>())
    .def(py::init< std::shared_ptr<SystemDefinition>, std::string, Scalar, Scalar, Scalar >())
    .def("implicit_function", &TPMSManifold::implicit_function)
    .def("derivative", &TPMSManifold::derivative)
    .def("returnL", &TPMSManifold::returnL)
    ;
    }
